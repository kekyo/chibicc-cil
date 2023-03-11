using System;
using System.Collections.Generic;
using System.Globalization;
using System.Runtime.InteropServices;
using System.Text;

// This will remain a simple implementation and will not be fully implemented.
// Because we will implement libc-cil in the near future,
// these codes will eventually become unnecessary.
namespace C
{
    public static class text
    {
        private const byte percent = (byte)'%';
        private const byte digit = (byte)'d';
        private const byte dot = (byte)'.';
        private const byte flot = (byte)'f';
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

                        var bytes = Encoding.UTF8.GetBytes(d.ToString(CultureInfo.InvariantCulture));
                        foreach (var b in bytes)
                        {
                            wr(b);
                        }
                        fmt++;
                    }
                    else if (*fmt == flot || *fmt == dot)
                    {
                        var sb = new List<byte>();
                        if (*fmt == dot)
                        {
                            fmt++;
                            while (*fmt != flot && *fmt != 0)
                            {
                                sb.Add(*fmt);
                                fmt++;
                            }
                        }
                        if (*fmt == flot)
                        {
                            // f = va_arg(ap, double);
                            var f = __refvalue(ap.GetNextArg(), double);

                            var width = Encoding.UTF8.GetString(sb.ToArray());
                            var bytes = Encoding.UTF8.GetBytes(f.ToString("F" + width, CultureInfo.InvariantCulture));
                            foreach (var b in bytes)
                            {
                                wr(b);
                            }
                            fmt++;
                        }
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

        public static unsafe int vsprintf(sbyte* buf, sbyte* fmt, ArgIterator ap)
        {
            var p = (byte*)buf;
            var len = 0;
            vwrprintf((byte*)fmt, ap, v =>
            {
                *p = v;
                p++;
                len++;
            });
            *p = 0;
            return len;
        }

        public static unsafe int sprintf(sbyte* buf, sbyte* fmt, __arglist)
        {
            // va_list ap;
            // va_start(ap, fmt);
            var ap = new ArgIterator(__arglist);
            return vsprintf(buf, fmt, ap);
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

        public static unsafe int getptrsize() =>
            sizeof(int*);

        public static unsafe void exit(int code) =>
            Environment.Exit(1);
    }
}
