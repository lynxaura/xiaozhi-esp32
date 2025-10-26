#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/soc_caps.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "analog.h"

const static char *TAG = "analog-adc";

// 测试定义宏
//#define ANGLE_USE_ADC2 
#define SWAP_BODY_ANGLE_CHAN
#define DEBUG_SAMPLE (1) 
#define LOG_PRINT_CONT (10) // 200ms * LOG_PRINT_CONT 

#define BATTERY_ADC2_CHAN       (ADC_CHANNEL_8) // IO19
#define BODY_TYPE_ADC2_CHAN     (ADC_CHANNEL_9) // IO20
#define ANGLE_SENSOR_ADC2_CHAN  (ADC_CHANNEL_0) // IO11 // 可调线使用ADC2
#define ANGLE_SENSOR_ADC1_CHAN  (ADC_CHANNEL_9) // IO10 

#define ADC_ATTEN                ADC_ATTEN_DB_0 // 0db衰减，1100mV ref，板载测试安全
#define ADC_BODY_TYPE_ATTEN      ADC_ATTEN_DB_6 // 带衰减

// ADC1 config
adc_cali_handle_t adc1_cali_angle_handle = NULL;
adc_oneshot_unit_handle_t adc1_handle;
adc_oneshot_unit_init_cfg_t init_config1 = {
    .unit_id = ADC_UNIT_1,
    .ulp_mode = ADC_ULP_MODE_DISABLE,
};

adc_oneshot_chan_cfg_t config_0 = {
    .atten = ADC_ATTEN,
    .bitwidth = ADC_BITWIDTH_12,
};

adc_oneshot_chan_cfg_t config_12 = {
    .atten = ADC_BODY_TYPE_ATTEN,
    .bitwidth = ADC_BITWIDTH_12,
};

// ADC2 config
adc_cali_handle_t adc2_cali_battery_handle = NULL;
adc_cali_handle_t adc2_cali_bodytype_handle = NULL;
adc_cali_handle_t adc2_cali_angle_handle = NULL;
adc_oneshot_unit_handle_t adc2_handle;
adc_oneshot_unit_init_cfg_t init_config2 = {    
    .unit_id = ADC_UNIT_2,
    .ulp_mode = ADC_ULP_MODE_DISABLE,
};

int adcSampleData[ALL_SAMPLE_NUMS] = { 0 }; // 采样原始值
int adcVolData[ALL_SAMPLE_NUMS] = { 0 }; // 校准后的电压值
int adcProData[ALL_SAMPLE_NUMS] = { 0 }; // 各模块实际处理好的数据

#define SWR_TICK_TO_MS (1000)
#define ADC_SAMPLE_PERIOD (200)
esp_timer_handle_t adcSampleTimerHd = nullptr;
unsigned int assitCnt = 0;

AngleSensor* m_angleSensor_ = nullptr;
void set_anglehd(AngleSensor* AngleSensor_) {
    m_angleSensor_ = AngleSensor_;
}

void SetAssitCnt(unsigned int num) {
    assitCnt = num;
}

unsigned int GetAssitCnt() {
    return assitCnt;
}

static bool AdcCalibrationInit(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle)
{
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (!calibrated) {
        ESP_LOGI(TAG, "calibration scheme version is %s", "Curve Fitting");
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = unit,
            .chan = channel,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (!calibrated) {
        ESP_LOGI(TAG, "calibration scheme version is %s", "Line Fitting");
        adc_cali_line_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }
#endif

    *out_handle = handle;
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Calibration Success");
    } else if (ret == ESP_ERR_NOT_SUPPORTED || !calibrated) {
        ESP_LOGW(TAG, "eFuse not burnt, skip software calibration");
    } else {
        ESP_LOGE(TAG, "Invalid arg or no memory");
    }

    return calibrated;
}
/* 处理电池采样数据 */
#define BAT_FILTER_CNT      (5)
#define K_BAT (11) // 分压 1K/(1K+10K)
int g_vbat = 0;
void ProcessBattery(int sampleVol) {
    static int vol = 0;
    static int cnt = 0;

    vol += sampleVol;
    cnt++;
    if (cnt == BAT_FILTER_CNT) {
        adcProData[BATTERY_SAMPLE] = (vol / BAT_FILTER_CNT);
        vol = 0;
        cnt = 0;
    }
}

int DRV_GetBatVol(void) {
    g_vbat = adcProData[BATTERY_SAMPLE] * K_BAT;
    return g_vbat;
}

/* 处理身体采样数据 */
#define BODY_FILTER_CNT (10)
#define ADJUST_FULL_MV_BODY (4950)
#define FULL_MV_BODY        (5000)
void ProcessBodytype(int sampleVol) {
    static int vol = 0;
    static int cnt = 0;

    vol += sampleVol * FULL_MV_BODY / ADJUST_FULL_MV_BODY;
    cnt++;
    if (cnt == BODY_FILTER_CNT) {
        adcProData[BODY_TYPE_SAMPLE] = (vol / BODY_FILTER_CNT);
        vol = 0;
        cnt = 0;
    }
}
/******
    类型名，类型下限电压，类型上限电压，打印信息
*******/
body_type_info body_info[ALL_BODY_TYPE_NUMS] = {
    { BODY_TYPE_1, 100, 150, "body type 1" },
    { BODY_TYPE_2, 150, 200, "body type 2" },
    { BODY_TYPE_3, 200, 250, "body type 3" },
    { BODY_TYPE_4, 250, 300, "body type 4" },
    { BODY_TYPE_5, 300, 350, "body type 5" },
    { BODY_TYPE_6, 350, 400, "body type 6" },
    { BODY_TYPE_7, 400, 450, "body type 7" },
    { BODY_TYPE_DEFAULT, 450, 500, "default body type 8" },
    { BODY_TYPE_9, 500, 550, "body type 9" },
    { BODY_TYPE_10, 550, 600, "body type 10" },
    { BODY_TYPE_11, 600, 650, "body type 11" },
    { BODY_TYPE_12, 650, 700, "body type 12" },
    { BODY_TYPE_13, 700, 750, "body type 13" },
    { BODY_TYPE_14, 750, 800, "body type 14" },
    { BODY_TYPE_15, 800, 850, "body type 15" },
    { BODY_TYPE_16, 850, 900, "body type 16" },
    { BODY_TYPE_17, 900, 950, "body type 17" },
    { BODY_TYPE_18, 950, 1000, "body type 18" },
};
bool DRV_GetBodyInserted(void) {
    if ((adcProData[BODY_TYPE_SAMPLE] >= body_info[BODY_TYPE_1].lowerVol)
        && (adcProData[BODY_TYPE_SAMPLE] <= body_info[ALL_BODY_TYPE_NUMS - 1].upperVol)) {
        return true;
    }

    return false;
}

body_type_def DRV_GetBodyType(void) {
    if (DRV_GetBodyInserted() == false) {
        return INVALID_TYPE;
    }

    int val = adcProData[BODY_TYPE_SAMPLE];
    int type = INVALID_TYPE;

    for (int i = BODY_TYPE_1; i < ALL_BODY_TYPE_NUMS; i++) {
        if ((val >= body_info[i].lowerVol) && (val <= body_info[i].upperVol)) {
            type = i;
            break;
        }
    }

    return (body_type_def)type;
}

/* 处理角度传感器数据 */
#define ANGLE_FILTER_CNT (5)
#define SENSOR_RES_TOTAL (84) // 规格书10K,实测满偏电压约750mV，修正值约8.4K 
#define DIV_RES_TOTAL    (470) // 47k
#define RES_TOTAL        (SENSOR_RES_TOTAL + DIV_RES_TOTAL)
#define FULL_VOL_MV      (4950)
int g_fullAngleVol = (FULL_VOL_MV * SENSOR_RES_TOTAL / RES_TOTAL); // 360°满偏电压
int g_angle = 0; // 0.1°
void ProcessAngle(int sampleVol) {
    static int vol = 0;
    static int cnt = 0;

    vol += sampleVol;
    cnt++;
    if (cnt == ANGLE_FILTER_CNT) {
        adcProData[ANGLE_SENSOR_SAMPLE] = (vol / ANGLE_FILTER_CNT);
        vol = 0;
        cnt = 0;
    }
}

int GetAngle(void) {
    g_angle = (360 * adcProData[ANGLE_SENSOR_SAMPLE] * 10 / g_fullAngleVol );
    return g_angle;
}

/* 处理角度传感器数据 */
#define TEST_FILTER_CNT (5)
void ProcessTest(int sampleVol) {
    static int vol = 0;
    static int cnt = 0;

    vol += sampleVol;
    cnt++;
    if (cnt == TEST_FILTER_CNT) {
        adcProData[ADC_TEST_SAMPLE] = (vol / TEST_FILTER_CNT);
        vol = 0;
        cnt = 0;
    }
}

// 200ms
void AdcSampleTimerHandle(void) {
    static unsigned int logPeriod = 0;
    static unsigned int backcenter = 0;
#ifdef ANGLE_USE_ADC2
    ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, ANGLE_SENSOR_ADC1_CHAN, &adcSampleData[ADC_TEST_SAMPLE]));
    ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc1_cali_angle_handle, adcSampleData[ADC_TEST_SAMPLE], &adcVolData[ADC_TEST_SAMPLE]));
    ProcessTest(adcVolData[ADC_TEST_SAMPLE]);

    ESP_ERROR_CHECK(adc_oneshot_read(adc2_handle, ANGLE_SENSOR_ADC2_CHAN, &adcSampleData[ANGLE_SENSOR_SAMPLE]));
    ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc2_cali_angle_handle, adcSampleData[ANGLE_SENSOR_SAMPLE], &adcVolData[ANGLE_SENSOR_SAMPLE]));
#ifdef SWAP_BODY_ANGLE_CHAN
    ProcessBodytype(adcVolData[ANGLE_SENSOR_SAMPLE]);
#else
    ProcessAngle(adcVolData[ANGLE_SENSOR_SAMPLE]);
#endif
#else
    ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, ANGLE_SENSOR_ADC1_CHAN, &adcSampleData[ANGLE_SENSOR_SAMPLE]));
    ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc1_cali_angle_handle, adcSampleData[ANGLE_SENSOR_SAMPLE], &adcVolData[ANGLE_SENSOR_SAMPLE]));
#ifdef SWAP_BODY_ANGLE_CHAN
    ProcessBodytype(adcVolData[ANGLE_SENSOR_SAMPLE]);
#else
    ProcessAngle(adcVolData[ANGLE_SENSOR_SAMPLE]);
#endif

    ESP_ERROR_CHECK(adc_oneshot_read(adc2_handle, ANGLE_SENSOR_ADC2_CHAN, &adcSampleData[ADC_TEST_SAMPLE]));
    ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc2_cali_angle_handle, adcSampleData[ADC_TEST_SAMPLE], &adcVolData[ADC_TEST_SAMPLE]));
    ProcessTest(adcVolData[ADC_TEST_SAMPLE]);
#endif
    ESP_ERROR_CHECK(adc_oneshot_read(adc2_handle, BATTERY_ADC2_CHAN, &adcSampleData[BATTERY_SAMPLE]));
    ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc2_cali_battery_handle, adcSampleData[BATTERY_SAMPLE], &adcVolData[BATTERY_SAMPLE]));
    ProcessBattery(adcVolData[BATTERY_SAMPLE]);

    ESP_ERROR_CHECK(adc_oneshot_read(adc2_handle, BODY_TYPE_ADC2_CHAN, &adcSampleData[BODY_TYPE_SAMPLE]));
    ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc2_cali_bodytype_handle, adcSampleData[BODY_TYPE_SAMPLE], &adcVolData[BODY_TYPE_SAMPLE]));
#ifdef SWAP_BODY_ANGLE_CHAN
    ProcessAngle(adcVolData[BODY_TYPE_SAMPLE]);
#else
    ProcessBodytype(adcVolData[BODY_TYPE_SAMPLE]);
#endif
    if (DEBUG_SAMPLE == 0) {
        return;
    }
    logPeriod++;
    if (logPeriod == LOG_PRINT_CONT) {
        body_type_def type = DRV_GetBodyType();
        ESP_LOGI(TAG, "\r\nBATTERY:%dmV - %dmV\r\nBODY:%dmV type:%s \r\nANGLE:%dmV - %d°\r\nTEST:%dmV", 
            adcProData[BATTERY_SAMPLE], DRV_GetBatVol(), 
            adcProData[BODY_TYPE_SAMPLE], ((type == INVALID_TYPE) ? "invalid" : body_info[type].infoP),
            adcProData[ANGLE_SENSOR_SAMPLE], GetAngle(),
            adcProData[ADC_TEST_SAMPLE]);
        logPeriod = 0;
    }

    if (m_angleSensor_ == nullptr) {
        return;
    }

    backcenter++;
    if (backcenter == 50) {
        // 10秒：顺时针转45度
        m_angleSensor_->SetRotate(CW_LEFT, 45);
    } else if (backcenter == 75) {
        // 15秒：逆时针转55度
        m_angleSensor_->SetRotate(CCW_RIGHT, 55);
    } else if (backcenter == 100) {
        // 20秒：回到中心位置
        m_angleSensor_->BackToCenter();
        backcenter = 0;
    }

    // 为应用获取准确数据做辅助延时
    if (assitCnt > 0) {
        assitCnt--;
    }
}

int DRV_GetSampleData(sample_channel channel) {
    return adcProData[channel];
}

void DRV_AdcInit(void) {
    // ADC1
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));
    if (AdcCalibrationInit(ADC_UNIT_1, ANGLE_SENSOR_ADC1_CHAN, ADC_BODY_TYPE_ATTEN, &adc1_cali_angle_handle) == false) {
        ESP_LOGW(TAG, "DRV_AdcInit ANGLE Cali ERR!");
    }
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ANGLE_SENSOR_ADC1_CHAN, &config_12));

    // ADC2
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config2, &adc2_handle));
    if (AdcCalibrationInit(ADC_UNIT_2, BATTERY_ADC2_CHAN, ADC_ATTEN, &adc2_cali_battery_handle) == false) {
        ESP_LOGW(TAG, "DRV_AdcInit BAT Cali ERR!");
    }
    if (AdcCalibrationInit(ADC_UNIT_2, BODY_TYPE_ADC2_CHAN, ADC_ATTEN, &adc2_cali_bodytype_handle) == false) {
        ESP_LOGW(TAG, "DRV_AdcInit BAT Cali ERR!");
    }
    if (AdcCalibrationInit(ADC_UNIT_2, ANGLE_SENSOR_ADC2_CHAN, ADC_BODY_TYPE_ATTEN, &adc2_cali_angle_handle) == false) {
        ESP_LOGW(TAG, "DRV_AdcInit ANGLE Cali ERR!");
    }
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc2_handle, ANGLE_SENSOR_ADC2_CHAN, &config_12));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc2_handle, BATTERY_ADC2_CHAN, &config_0));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc2_handle, BODY_TYPE_ADC2_CHAN, &config_0));

    // 创建定时器，200ms处理一次数据
    ESP_ERROR_CHECK(adc_oneshot_read(adc2_handle, BATTERY_ADC2_CHAN, &adcSampleData[0]));
    ESP_LOGI(TAG, "DRV_AdcInit OK!Test BATTERY_SAMPLE:%d", adcSampleData[0]);

    esp_timer_create_args_t adc_sample_timer_args = {
        .callback = [](void* arg) {
            AdcSampleTimerHandle();
        },
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "adc_sample_timer",
        .skip_unhandled_events = true,
    };
    esp_timer_create(&adc_sample_timer_args, &adcSampleTimerHd);
    esp_timer_start_periodic(adcSampleTimerHd, ADC_SAMPLE_PERIOD * SWR_TICK_TO_MS);

    return;
}

/* 角度传感器控制，归到analog控制部分 */
AngleSensor::AngleSensor(Motion* motion)
    : m_motion(motion), m_centerPosAng(BODY_CENTER_ANGLE), m_toleranceAng(TOLERANCE_ANGLE),
      m_angleNow(0) {
}

AngleSensor::~AngleSensor() {
}

void AngleSensor::FreshAngle(void) {
    m_angleNow = GetAngle();
}

void AngleSensor::SetRotate(rotate_dir dir, int angle) {
    // 按照齿比将“玩具角度”转换为“马达相对角度”
    // transAngle 单位为 0.1°（马达），ftransAngle 为 0.1°（带符号）
    int transAngle = (angle * 10 * m_bigCircle) / m_smallCircle;
    float ftransAngle = (dir == CW_LEFT) ? ((float)(transAngle)) : (-(float)(transAngle));
    ESP_LOGW(TAG, "SetRotate big:%d° - %f°/10 (relative)", angle, ftransAngle);
    // 使用相对角度接口，单位：度（马达）
    m_motion->MotorTurnByAngle((ftransAngle / 10), MOTION_SPEED_FAST);
    m_motion->StopMotor();
}

void AngleSensor::BackToCenter(void) {
    FreshAngle();
    int bigangle;
    int transAngle = 0;
    float ftransAngle = 0;
    if ((m_angleNow >= 0) && (m_angleNow <= (m_centerPosAng + m_toleranceAng - 3600))) {
        // 靠近0度一侧，最短路径应为负向回正
        bigangle = m_angleNow + 3600 - m_centerPosAng;
        transAngle = (bigangle * m_bigCircle) / m_smallCircle;
        ftransAngle = -(float)(transAngle);
    } else if ((m_angleNow >= m_centerPosAng) && (m_angleNow <= 3600)) {
        // 在中心右侧，应为负向回正
        bigangle = (m_angleNow - m_centerPosAng);
        transAngle = (bigangle * m_bigCircle) / m_smallCircle;
        ftransAngle = -(float)(transAngle);
    } else if ((m_angleNow >= (m_centerPosAng - m_toleranceAng)) && (m_angleNow <= m_centerPosAng)) {
        // 在中心左侧，应为正向回正
        bigangle = (m_centerPosAng - m_angleNow);
        transAngle = (bigangle * m_bigCircle) / m_smallCircle;
        ftransAngle = +(float)(transAngle);
    } else {
        ESP_LOGW(TAG, "BackToCenter INVALID Angle!");
        return;
    }
    ESP_LOGW(TAG, "BackToCenter big:%d - %f°/10 (relative)", bigangle, ftransAngle);
    // 使用相对角度接口，单位：度（马达）
    m_motion->MotorTurnByAngle((ftransAngle / 10), MOTION_SPEED_MEDIUM);
    m_motion->StopMotor();
}
