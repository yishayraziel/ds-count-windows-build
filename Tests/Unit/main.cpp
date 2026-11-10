#include "TestHarness.h"
#include <cstring>

int main (int argc, char** argv)
{
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int ran = 0;
    for (auto& c : test::cases())
    {
        if (filter != nullptr && std::strcmp (filter, "--report") != 0 && std::strstr (c.name, filter) == nullptr)
            continue;
        const int before = test::failures();
        c.fn();
        std::printf ("[%s] %s\n", test::failures() == before ? " ok " : "FAIL", c.name);
        ++ran;
    }
    std::printf ("\n%d tests, %d checks, %d failures\n", ran, test::checks(), test::failures());
    if (FILE* f = std::fopen ("dscount_results.csv", "w"))
    {
        for (auto& [k, v] : test::results())
            std::fprintf (f, "%s,%.6f\n", k.c_str(), v);
        std::fclose (f);
    }
    return test::failures() == 0 ? 0 : 1;
}
