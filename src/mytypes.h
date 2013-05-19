// Our favourite types!

typedef unsigned int u32;
typedef signed int s32;
typedef unsigned short u16;
typedef signed short s16;
typedef unsigned char u8;
typedef signed char s8;

#define ABS(a) (((a)<0) ? -(a) : (a))
#define MAX(a, b) (((a)<(b)) ? (b) : (a))
#define MIN(a, b) (((a)<(b)) ? (a) : (b))