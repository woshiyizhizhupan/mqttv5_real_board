/*
 * Copyright (c) 2006-2024, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2024-09-02     RT-Thread    first version
 */
#if defined(__CC_ARM)
#include "stdint.h"
#include "stdio.h"

/* 重定向fputc函数 */
int fputc(int ch, FILE *f)
{
    /* 在这里实现你的串口发送一个字节的功能 */
    // 例如 HAL 库: HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
    // 例如 寄存器: while((USART1->SR & 0X40)==0); USART1->DR = (uint8_t)ch;
    extern void print_char(uint8_t ch);
    print_char((char)ch);

    return ch;
}

#elif defined(__GNUC__)

#include <stdio.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <errno.h>

#undef errno
extern int errno;
extern char _end;
extern char __HeapLimit;
extern void print_char(char);
caddr_t _sbrk(int incr)
{
    static unsigned char *heap = NULL;
    unsigned char *prev_heap;

    if (heap == NULL)
    {
        heap = (unsigned char *)&_end;
    }
    prev_heap = heap;

    if ((incr > 0) && ((heap + incr < heap) || (heap + incr > (unsigned char *)&__HeapLimit)))
    {
        errno = ENOMEM;
        return (caddr_t)-1;
    }
    if ((incr < 0) && ((unsigned int)(-incr) > (unsigned int)(heap - (unsigned char *)&_end)))
    {
        errno = ENOMEM;
        return (caddr_t)-1;
    }

    heap += incr;

    return (caddr_t)prev_heap;
}

int link(char *old, char *new)
{
    return -1;
}

int _close(int file)
{
    return -1;
}

int _fstat(int file, struct stat *st)
{
    st->st_mode = S_IFCHR;
    return 0;
}

int _isatty(int file)
{
    return 1;
}

int _lseek(int file, int ptr, int dir)
{
    return 0;
}

int _read(int file, char *ptr, int len)
{
    return 0;
}

void abort(void)
{
    /* Abort called */
    while (1)
        ;
}

int _write(int fd, char *pBuffer, int size)
{
    for (int i = 0; i < size; i++)
    {
        // if (pBuffer[i] == '\n')
        // {
        //    print_char('\r');
        // }
        print_char(pBuffer[i]);
    }
    return size;
}

#endif
