#ifndef _SDDATA_PRO_
#define _SDDATA_PRO_

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/sdmmc_host.h"

/* CONFIG PATHS REMOVED: vasys deprecated */

class SDdata_Pro {
public:
    SDdata_Pro();
    ~SDdata_Pro();

    sdmmc_card_t *m_card;

    void TestFile(); // 测试读写
private:
    esp_err_t TWriteFile(const char *path, char *data);
    esp_err_t TReadFile(const char *path);
};

SDdata_Pro* SDmoduleInit(void);
SDdata_Pro* GetSDHandle(void);
void SDLoadImageTest(void);

#endif /* _SDDATA_PRO_ */
