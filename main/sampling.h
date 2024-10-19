#ifndef _SAMPLING_H
#define _SAMPLING_H

#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SPS CONFIG_TINYECG_SPS

#if defined(CONFIG_TINYECG_FPS_25)
# define FPS 25
#elif defined(CONFIG_TINYECG_FPS_30)
# define FPS 30
#else
# error "Must define FPS, either 25 or 30"
#endif

#if (configTICK_RATE_HZ % FPS)
# error "TICK_RATE_HZ must be a multiple of FPS"
#endif
#if (SPS % FPS)
# error "SPS must be a multiple of FPS"
#endif

#ifdef __cplusplus
}
#endif

#endif /* _SAMPLING_H */
