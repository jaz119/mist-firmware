#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "mist_cfg.h"

int main()
{
    memset(&mist_cfg, 0, sizeof(mist_cfg));
    ini_parse(&mist_ini_cfg, "DEFENDER", 0);

    for (int i=0; i<5; i++) {
        printf("minimig cfg[%d] = %s\n", i, minimig_cfg.conf_name[i]);
    }

    for (int i=0; i<5; i++) {
        printf("atarist cfg[%d] = %s\n", i, atarist_cfg.conf_name[i]);
    }
}
