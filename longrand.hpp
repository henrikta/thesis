#pragma once

unsigned long longrand()
{
    return static_cast<unsigned long>(rand()) |
        (static_cast<unsigned long>(rand()) << 32);
}

