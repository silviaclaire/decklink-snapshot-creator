#pragma once

#include <iostream>
#include <string>
#include <time.h>

namespace Utils
{
    // Get formatted current date/time
    const std::string CurrentDateTime(const char* fmt)
    {
        time_t     now = time(NULL);
        struct tm  tstruct;
        char       buf[30];

        localtime_s(&tstruct, &now);
        strftime(buf, sizeof(buf), fmt, &tstruct);
        return buf;
    }
}
