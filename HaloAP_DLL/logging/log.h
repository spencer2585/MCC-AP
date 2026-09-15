#pragma once

#include <format>
#include <source_location>
#include <string>
#include <string_view>
#include <cstdio>

namespace haloap
{
    struct formatWithLoc
    {
        std::string_view str;
        std::source_location location;
    
        formatWithLoc(const char* s, std::source_location loc = std::source_location::current()):str(s), location(loc){}
    };
    template <typename... Args>
    void Log(formatWithLoc fmt, Args... args)
    {
        //Construct our message
        std::string message = std::vformat(fmt.str, std::make_format_args(args...));
        //append location to beginning
        std::string finalMessage = std::format("[{}:{}] {}\n", fmt.location.file_name(), fmt.location.line(), message );
        //write to console
#ifdef HALOAP_ENABLE_CONSOLE
        fputs(finalMessage.c_str(), stdout);
#endif
        //TODO: write to file
    }
}