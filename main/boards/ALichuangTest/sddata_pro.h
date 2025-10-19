#ifndef _SDDATA_PRO_
#define _SDDATA_PRO_

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/sdmmc_host.h"

/* CONFIG PATH */
#define VASYS_CFG_PATH    "/sdcard/config/"

/* SOUND PATH */
#define TEST_OGG_PATH    "/sdcard/welcome.ogg"

class SDdata_Pro {
public:
    SDdata_Pro();
    ~SDdata_Pro();

    sdmmc_card_t *m_card;

    void TestFile(); // 测试读写
    int GetVASysConfig(char* databuff);
private:
    esp_err_t TWriteFile(const char *path, char *data);
    esp_err_t TReadFile(const char *path);
};

SDdata_Pro* SDmoduleInit(void);
SDdata_Pro* GetSDHandle(void);
void SDLoadImageTest(void);

#endif /* _SDDATA_PRO_ */