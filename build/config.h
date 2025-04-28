#ifndef __CONFIG_H_
#define __CONFIG_H_

#define IS_BIGENDIAN 0
#define INT128_SIZE 

#ifdef __cplusplus
inline bool is_bigendian() {
  return IS_BIGENDIAN;
}
#endif  //__cplusplus

/* #undef OS_WINDOWS */
/* #undef OS_LINUX */
/* #undef OS_DARWIN */
/* #undef OS_FREEBSD */
/* #undef OS_COMMONUNIX */

#define CMAKE_INSTALL_PREFIX "/home/danjuan/install/x86_64-Linux"

#endif //__CONFIG_H_
