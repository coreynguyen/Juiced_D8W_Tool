/*───────────────────────────────────────────────────────────────
   main.cpp   –  GUI / CLI bootstrap for d8t + d8w toolset
   (pre-C++11 compliant)
  ──────────────────────────────────────────────────────────────*/
#include <windows.h>
#include <direct.h>
#include <algorithm>
#include <iostream>
#include <vector>
#include <string>
#include <cstring>

#include <wx/wx.h>              /* GUI */
#include "d8wTool.h"            /* wxWidgets front-end */

#include "d8w_parser.h"         /* D8TFile, D8WBank */
#include "resource.h"

wxIMPLEMENT_APP_NO_MAIN(d8wToolApp);

/*──── handy typedefs ──────────────────────────────────────────*/
using juiced::D8TFile;
using juiced::D8WBank;

/*──────────────────────── helpers ─────────────────────────────*/
static void printUsage()
{
    std::cout <<
      "Usage (CLI):\n"
      "  -export      <d8t> <d8w> <pack> <idx> <out.ddt>\n"
      "  -exportset   <d8t> <d8w> <pack> <outDir>\n"
      "  -convert     <d8t> <d8w> <pack> <idx> <out.dds>\n"
      "  -convertset  <d8t> <d8w> <pack> <outDir>\n"
      "  -import      <d8t> <d8w> <pack> <idx> <in.ddt>\n"
      "  -importset   <d8t> <d8w> <pack> <inDir>\n";
}

/* simple atoi with range-check */
static bool parseUint(const char* s, size_t& out)
{
    char* end = 0;
    long v = ::strtol(s, &end, 10);
    if (!*s || *end || v < 0) return false;
    out = static_cast<size_t>(v);
    return true;
}

/* bail-out helper (pre-C++11, no lambda) */
static int bail(const char* msg)
{
    std::cout << msg << '\n';
    return 3;
}

/*────────────────────── CLI runner ───────────────────────────*/
static int runCLI(int argc, char** argv)
{
    if (argc < 2) { printUsage(); return 1; }

    const std::string verb = argv[1];
    if (verb == "-h" || verb == "--help") { printUsage(); return 0; }

    /* all verbs need at least <d8t> <d8w> */
    if (argc < 4) { printUsage(); return 1; }

    /* 1) load .d8t */
    D8TFile big;
    if (!big.load(argv[2]))
        return bail("failed to load .d8t");

    /* 2) load one .d8w that references the shared buffer */
    D8WBank bank;
    if (!bank.load(argv[3], big.buffer()))
        return bail("failed to load .d8w");

    /*──────── verb dispatch ────────*/
    if (verb == "-export" && argc == 7)
    {
        size_t pack, idx;
        if (!parseUint(argv[4], pack) || !parseUint(argv[5], idx))
            { printUsage(); return 1; }

        if (!bank.exportTexture(pack, idx, argv[6]))
            return bail("export failed");
        return 0;
    }
    else if (verb == "-exportset" && argc == 6)
    {
        size_t pack;
        if (!parseUint(argv[4], pack)) { printUsage(); return 1; }

        if (!bank.exportTextureSet(pack, argv[5]))
            return bail("exportset failed");
        return 0;
    }
    else if (verb == "-convert" && argc == 7)
    {
        size_t pack, idx;
        if (!parseUint(argv[4], pack) || !parseUint(argv[5], idx))
            { printUsage(); return 1; }

        if (!bank.convertTexture(pack, idx, argv[6]))
            return bail("convert failed");
        return 0;
    }
    else if (verb == "-convertset" && argc == 6)
    {
        size_t pack;
        if (!parseUint(argv[4], pack)) { printUsage(); return 1; }

        if (!bank.convertTextureSet(pack, argv[5]))
            return bail("convertset failed");
        return 0;
    }
    else if (verb == "-import" && argc == 7)
    {
        size_t pack, idx;
        if (!parseUint(argv[4], pack) || !parseUint(argv[5], idx))
            { printUsage(); return 1; }

        if (!bank.importTexture(pack, idx, argv[6]))
            return bail("import failed");

        if (bank.isDirty())
            bank.save(argv[3], argv[2]);   /* write .d8w & .d8t */
        return 0;
    }
    else if (verb == "-importset" && argc == 6)
    {
        size_t pack;
        if (!parseUint(argv[4], pack)) { printUsage(); return 1; }

        if (!bank.importTextureSet(pack, argv[5]))
            return bail("importset failed");

        if (bank.isDirty())
            bank.save(argv[3], argv[2]);
        return 0;
    }

    printUsage();
    return 1;
}

/*────────────────────── program entry ────────────────────────*/
int main(int argc, char** argv)
{
    /* CLI mode if first arg starts with '-' */
    if (argc > 1 && argv[1][0] == '-')
        return runCLI(argc, argv);

    /* otherwise launch wxWidgets GUI */
    return wxEntry(argc, argv);
}
