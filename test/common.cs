using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

namespace C;

// This will remain a simple implementation and will not be fully implemented.
// Because we will implement libc-cil in the near future,
// these codes will eventually become unnecessary.
public static class text
{
    private const byte percent = (byte)'%';
    private const byte digit = (byte)'d';
    private const byte str = (byte)'s';

    private static unsafe void vwrprintf(
        byte* fmt, /* va_list */ ArgIterator ap, Action<byte> wr)
    {
        while (*fmt != 0)
        {
            if (*fmt == percent)
            {
                fmt++;
                if (*fmt == digit)
                {
                    // d = va_arg(ap, int);
                    var d = __refvalue(ap.GetNextArg(), int);

                    var bytes = Encoding.UTF8.GetBytes(d.ToString());
                    foreach (var b in bytes)
                    {
                        wr(b);
                    }
                    fmt++;
                }
                else if (*fmt == str)
                {
                    // d = va_arg(ap, char*);
                    var pstr = (byte*)__refvalue(ap.GetNextArg(), sbyte*);

                    if (pstr != (byte*)0)
                    {
                        while (*pstr != 0)
                        {
                            wr(*pstr);
                            pstr++;
                        }
                    }
                    fmt++;
                }
                else if (*fmt == 0)
                {
                    break;
                }
                else
                {
                    wr(*fmt);
                    fmt++;
                }
            }
            else if (*fmt == 0)
            {
                break;
            }
            else
            {
                wr(*fmt);
                fmt++;
            }
        }
    }

    public static unsafe int printf(sbyte* fmt, __arglist)
    {
        // va_list ap;
        // va_start(ap, fmt);
        var ap = new ArgIterator(__arglist);

        var buffer = new List<byte>();
        vwrprintf((byte*)fmt, ap, buffer.Add);
        var str = Encoding.UTF8.GetString(buffer.ToArray());
        Console.Write(str);

        // va_end(ap);
        return buffer.Count;
    }

    public static unsafe int sprintf(sbyte* buf, sbyte* fmt, __arglist)
    {
        // va_list ap;
        // va_start(ap, fmt);
        var ap = new ArgIterator(__arglist);

        var p = (byte*)buf;
        var len = 0;
        vwrprintf((byte*)fmt, ap, v =>
        {
            *p = v;
            p++;
            len++;
        });

        // va_end(ap);
        return len;
    }

    public static unsafe int memcmp(sbyte* p, sbyte* q, long n)
    {
        while (n > 0)
        {
            int num = *p - *q;
            if (num != 0)
            {
                return num;
            }
            p++;
            q++;
            n--;
        }
        return 0;
    }

    public static unsafe int strcmp(sbyte* p, sbyte* q)
    {
        while (true)
        {
            int num = *p - *q;
            if (num != 0)
            {
                return num;
            }
            if (*p == 0)
            {
                break;
            }
            p++;
            q++;
        }
        return 0;
    }

    public static int getptrsize() =>
        IntPtr.Size;

    public static unsafe void assert(int expected, int actual, sbyte* code)
    {
        if (expected == actual)
        {
            Console.WriteLine("{0} => {1}", Marshal.PtrToStringAnsi((IntPtr)code), actual);
            return;
        }
        Console.WriteLine("{0} => {1} expected but got {2}", Marshal.PtrToStringAnsi((IntPtr)code), expected, actual);
        Environment.Exit(1);
    }

    public static unsafe int add_all(int n, __arglist)
    {
        var ap = new ArgIterator(__arglist);

        var sum = 0;
        for (var i = 0; i < n; i++)
            sum += __refvalue(ap.GetNextArg(), int);
        return sum;
    }
}
