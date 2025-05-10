//──────────────────────────────────────────────────────────────
//  d8w_parser.cpp   –   Win-32   (pre-C++11 compatible)
//──────────────────────────────────────────────────────────────
#include "d8w_parser.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <io.h>          // _mkdir
#include <direct.h>      // _mkdir
#include <algorithm>    // for std::sort
#include <sstream>


// DDS constants
#define DDS_MAGIC        0x20534444  // "DDS "
#define DDSD_CAPS        0x1
#define DDSD_HEIGHT      0x2
#define DDSD_WIDTH       0x4
#define DDSD_PITCH       0x8
#define DDSD_PIXELFORMAT 0x1000
#define DDSD_MIPMAPCOUNT 0x20000
#define DDSD_LINEARSIZE  0x80000

#define DDPF_ALPHAPIXELS 0x1
#define DDPF_FOURCC      0x4
#define DDPF_RGB         0x40

#define DDSCAPS_TEXTURE  0x1000
#define DDSCAPS_COMPLEX  0x8
#define DDSCAPS_MIPMAP   0x400000


//──────────────────────────────────────────────────────────────
//  Little-endian helpers  (host assumed little-endian)
//──────────────────────────────────────────────────────────────
template<typename T>
T juiced::LEread(const BYTE*& p)
{
    T v;
    memcpy(&v, p, sizeof(T));
    p += sizeof(T);
    return v;
}

template<typename T>
void juiced::LEwrite(BYTE*& p, T v)
{
    memcpy(p, &v, sizeof(T));
    p += sizeof(T);
}

static void appendLE32(std::vector<BYTE>& v, uint32_t x)
{
    v.push_back( (BYTE)( x        & 0xFF) );
    v.push_back( (BYTE)((x >>  8) & 0xFF) );
    v.push_back( (BYTE)((x >> 16) & 0xFF) );
    v.push_back( (BYTE)((x >> 24) & 0xFF) );
}


using namespace juiced;

//──────────────────────── ctor / dtor ─────────────────────────
D8WFile::D8WFile()  {}
D8WFile::~D8WFile() {}

//──────────────────────── helpers ─────────────────────────────
bool D8WFile::loadFileToMem(const std::string& path,
                            std::vector<BYTE>& buf) const
{
    HANDLE h = CreateFileA(path.c_str(), GENERIC_READ,
                           FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER sz;
    if (!GetFileSizeEx(h, &sz)) { CloseHandle(h); return false; }

    buf.resize(static_cast<size_t>(sz.QuadPart));
    DWORD br = 0;
    BOOL ok = ReadFile(h, &buf[0], (DWORD)buf.size(), &br, NULL);
    CloseHandle(h);
    return ok && br == buf.size();
}

bool D8WFile::locateD8T(const std::string& folder,
                        const std::string& stem,
                        const std::string& hint,
                        std::string& outPath) const
{
    if (!hint.empty()) { outPath = hint; return true; }

    // 1) same stem.d8t
    outPath = folder + "\\" + stem + ".d8t";
    if (GetFileAttributesA(outPath.c_str()) != INVALID_FILE_ATTRIBUTES)
        return true;

    // 2) first .d8t in folder
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((folder + "\\*.d8t").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        outPath = folder + "\\" + fd.cFileName;
        FindClose(h);
        return true;
    }
    return false;
}

//──────────────────────── load() ──────────────────────────────
bool D8WFile::load(const std::string& wPath,
                   const std::string& tHint)
{
    // ---- split folder & stem from wPath
    size_t posSlash = wPath.find_last_of("\\/");
    std::string folder = (posSlash == std::string::npos)
                         ? "." : wPath.substr(0, posSlash);

    std::string filename = (posSlash == std::string::npos)
                           ? wPath : wPath.substr(posSlash + 1);

    size_t posDot = filename.find_last_of('.');
    std::string stem = (posDot == std::string::npos)
                       ? filename : filename.substr(0, posDot);

    if (!locateD8T(folder, stem, tHint, pathT_))
        return false;

    if (!loadFileToMem(wPath, wBuf_)) return false;
    if (!loadFileToMem(pathT_, tBuf_)) return false;
    pathW_ = wPath;

    // ---- parse .d8w
    const BYTE* p   = &wBuf_[0];
    const BYTE* end = p + wBuf_.size();

    if ((size_t)(end - p) < 12) return false;
    uint32_t totalTex = LEread<uint32_t>(p);
    uint32_t tblCnt   = LEread<uint32_t>(p);
    uint32_t totalSz  = LEread<uint32_t>(p);

    // parse texture tables
    texBuf_.resize(tblCnt);
    for (size_t i = 0; i < tblCnt; ++i)
    {
        if ((size_t)(end - p) < 12) return false;
        TextureTable& tbl = texBuf_[i];
        tbl.off  = LEread<uint32_t>(p);
        tbl.size = LEread<uint32_t>(p);
        uint32_t n = LEread<uint32_t>(p);

        if ((size_t)(end - p) < n * sizeof(TextureHdr)) return false;
        tbl.tex.resize(n);
        for (size_t j = 0; j < n; ++j) {
            memcpy(&tbl.tex[j], p, sizeof(TextureHdr));
            p += sizeof(TextureHdr);
        }
    }

    // parse texture-set section
    if ((size_t)(end - p) < 4) return false;
    uint32_t setCnt = LEread<uint32_t>(p);
    texSet_.resize(setCnt);
    if (setCnt)
    {
        const BYTE* posSet = p;
        if ((size_t)(end - p) < 32) return false;

        uint32_t firstName = LEread<uint32_t>(p);
        p += 28;  // skip remainder of 32-byte name field

        uint32_t stride = 0;
        {
            const BYTE* probe = p;
            while (probe + 4 <= end) {
                uint32_t val = LEread<uint32_t>(probe);
                if (val == firstName) {
                    stride = (uint32_t)((probe - posSet - 36) / 4);
                    break;
                }
            }
        }
        if (stride == 0 && setCnt > 1) return false;

        // now actually read each TextureSet
        p = posSet;
        for (size_t i = 0; i < setCnt; ++i)
        {
            char nameBuf[33] = {0};
            memcpy(nameBuf, p, 32);
            texSet_[i].name = nameBuf;
            p += 32;

            texSet_[i].indexTable.resize(stride);
            for (size_t j = 0; j < stride; ++j)
                texSet_[i].indexTable[j] = LEread<int32_t>(p);
        }
    }

    // remainder = raw tail
    tailRaw_.assign(p, end);
    return true;
}

//──────────────────────── save() stub ─────────────────────────


bool D8WFile::save(const std::string& outW, const std::string& outT)
{
    if(!dirty_) return false;               // nothing to do

    // ── (1) rebuild fresh wBufOut in memory ──────────────────
    std::vector<BYTE> wOut;
    wOut.reserve(wBuf_.size());             // rough guess

    // header
    uint32_t totalTexCnt = 0;
    for(size_t p=0;p<texBuf_.size();++p) totalTexCnt += texBuf_[p].tex.size();
    uint32_t bufCnt  = (uint32_t)texBuf_.size();
    uint32_t totalSz = 0;                   // unknown – keep original
    //LEwrite<uint32_t>( (BYTE*&)wOut.insert(wOut.end(),sizeof(uint32_t),0) - &wOut[0], totalTexCnt);
    //LEwrite<uint32_t>( (BYTE*&)wOut.insert(wOut.end(),sizeof(uint32_t),0) - &wOut[0], bufCnt     );
    //LEwrite<uint32_t>( (BYTE*&)wOut.insert(wOut.end(),sizeof(uint32_t),0) - &wOut[0], totalSz    );
    appendLE32(wOut, totalTexCnt);
    appendLE32(wOut, bufCnt);
    appendLE32(wOut, totalSz);

    // texture tables
    for(size_t p=0;p<texBuf_.size();++p)
    {
        const TextureTable& tbl = texBuf_[p];
        //LEwrite<uint32_t>( (BYTE*&)wOut.insert(wOut.end(),sizeof(uint32_t),0)-&wOut[0], tbl.off );
        //LEwrite<uint32_t>( (BYTE*&)wOut.insert(wOut.end(),sizeof(uint32_t),0)-&wOut[0], tbl.size);
        //LEwrite<uint32_t>( (BYTE*&)wOut.insert(wOut.end(),sizeof(uint32_t),0)-&wOut[0],
        //                   (uint32_t)tbl.tex.size() );
        appendLE32(wOut, tbl.off);
        appendLE32(wOut, tbl.size);
        appendLE32(wOut, (uint32_t)tbl.tex.size());

        for(size_t i=0;i<tbl.tex.size();++i)
        {
            const TextureHdrEx& h = tbl.tex[i];
            const BYTE* src = (const BYTE*)&h;
            wOut.insert(wOut.end(), src, src+sizeof(TextureHdr));
        }
    }

    // texture-set section  (just copy original bytes)
    wOut.insert(wOut.end(), wBuf_.begin() + (texBuf_.empty()?12:         // start-of-set offset
                         (size_t)((BYTE*)&texBuf_.back().tex.back()+sizeof(TextureHdr)- &wBuf_[0])),
                wBuf_.end() - tailRaw_.size());

    // tail raw
    wOut.insert(wOut.end(), tailRaw_.begin(), tailRaw_.end());

    // ── (2) write both files with Win32 API ──────────────────
    auto writeWhole = [](const std::string& path,const std::vector<BYTE>& buf)->bool
    {
        HANDLE h = CreateFileA(path.c_str(),GENERIC_WRITE,0,nullptr,
                               CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(h==INVALID_HANDLE_VALUE) return false;
        DWORD bw; BOOL ok=WriteFile(h,&buf[0],(DWORD)buf.size(),&bw,nullptr);
        CloseHandle(h); return ok && bw==buf.size();
    };
    if(!writeWhole(outW,wOut)) return false;
    if(!writeWhole(outT,tBuf_)) return false;

    // ── (3) clear modified flags ─────────────────────────────
    for(size_t p=0;p<texBuf_.size();++p)
        for(size_t i=0;i<texBuf_[p].tex.size();++i)
            texBuf_[p].tex[i].modified = false;
    dirty_ = false;
    return true;
}


//──────────────────────── queries ─────────────────────────────
size_t D8WFile::texturePackCount() const { return texBuf_.size(); }
size_t D8WFile::textureCount(size_t p) const
{
    return (p < texBuf_.size()) ? texBuf_[p].tex.size() : 0;
}
const TextureHdr& D8WFile::texture(size_t p, size_t i) const
{
    return texBuf_[p].tex[i];
}

//──────────────────────── exportTexture ────────────────────────
static bool createDirIfMissing(const std::string& dir)
{
    if (GetFileAttributesA(dir.c_str()) == INVALID_FILE_ATTRIBUTES)
        return _mkdir(dir.c_str()) == 0;
    return true;
}

bool D8WFile::exportTexture(size_t pack, size_t idx,
                            const std::string& outFile) const
{
    if (pack >= texBuf_.size() || idx >= texBuf_[pack].tex.size())
        return false;

    const TextureTable& tbl = texBuf_[pack];
    const TextureHdr&   hdr = tbl.tex[idx];

    // compute offset in tBuf_
    size_t off = tbl.off;
    for (size_t k = 0; k < idx; ++k) off += tbl.tex[k].size;

    if (off + hdr.size > tBuf_.size()) return false;

    HANDLE h = CreateFileA(outFile.c_str(), GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;

    DWORD bw;
    WriteFile(h, ((BYTE*)&hdr) + 4, sizeof(TextureHdr) - 4, &bw, NULL);
    WriteFile(h, &tBuf_[off], hdr.size, &bw, NULL);
    CloseHandle(h);
    return true;
}

//──────────────────────── exportTextureSet ────────────────────
bool D8WFile::exportTextureSet(size_t pack,
                               const std::string& dir) const
{
    if (pack >= texBuf_.size()) return false;
    if (!createDirIfMissing(dir)) return false;

    char buf[260];
    size_t n = texBuf_[pack].tex.size();
    for (size_t i = 0; i < n; ++i)
    {
        sprintf_s(buf, "%s\\Tex%d%04d.ddt", dir.c_str(),
                  (int)pack, (int)i);
        if (!exportTexture(pack, i, buf)) return false;
    }
    return true;
}

//──────────────────────────────────────────────────────────────
// writeDDSHeader()
//──────────────────────────────────────────────────────────────
static bool writeDDSHeader(HANDLE h, const TextureHdr& hdr)
{
    // Prepare a 128‐byte header buffer
    BYTE buf[128] = {};
    BYTE* p = buf;

    // magic
    *(DWORD*)p = DDS_MAGIC;  p += 4;

    // DDS_HEADER:
    DWORD dwSize = 124;
    memcpy(p, &dwSize, 4);                    p += 4;

    // flags
    DWORD flags = DDSD_CAPS|DDSD_HEIGHT|DDSD_WIDTH|DDSD_PIXELFORMAT|DDSD_LINEARSIZE;
    if (hdr.mipCnt > 1) flags |= DDSD_MIPMAPCOUNT;
    memcpy(p, &flags, 4);                     p += 4;

    // height & width
    memcpy(p, &hdr.height, 4);                p += 4;
    memcpy(p, &hdr.width, 4);                 p += 4;

    // pitchOrLinearSize
    DWORD linearSize = hdr.size;
    memcpy(p, &linearSize, 4);                p += 4;

    // depth
    DWORD depth = 0;
    memcpy(p, &depth, 4);                     p += 4;

    // mipMapCount
    DWORD mipCount = hdr.mipCnt;
    memcpy(p, &mipCount, 4);                  p += 4;

    // reserved1[11]
    p += 11 * 4;

    // DDS_PIXELFORMAT (32 bytes)
    DWORD pfSize  = 32; memcpy(p, &pfSize,4); p += 4;

    DWORD pfFlags;
    if (hdr.type == 0x31545844 || hdr.type == 0x35545844) // 'DXT1' or 'DXT5'
        pfFlags = DDPF_FOURCC;
    else
        pfFlags = DDPF_RGB|DDPF_ALPHAPIXELS;
    memcpy(p, &pfFlags,4);                    p += 4;

    // FourCC or 0
    memcpy(p, &hdr.type,4);                   p += 4;

    // RGB bit count & masks
    DWORD rgbCount=0, rMask=0, gMask=0, bMask=0, aMask=0;
    if (!(pfFlags & DDPF_FOURCC)) {
        rgbCount=32;
        rMask=0x00FF0000; gMask=0x0000FF00;
        bMask=0x000000FF; aMask=0xFF000000;
    }
    memcpy(p, &rgbCount,4); p += 4;
    memcpy(p, &rMask,4);    p += 4;
    memcpy(p, &gMask,4);    p += 4;
    memcpy(p, &bMask,4);    p += 4;
    memcpy(p, &aMask,4);    p += 4;

    // caps
    DWORD caps = DDSCAPS_TEXTURE;
    if (hdr.mipCnt>1) caps |= DDSCAPS_COMPLEX|DDSCAPS_MIPMAP;
    memcpy(p, &caps,4);     p += 4;

    // caps2,3,4, reserved2
    p += 4*4;

    // write the header
    DWORD bw;
    if (!WriteFile(h, buf, 128, &bw, NULL) || bw != 128) return false;
    return true;
}

//──────────────────────────────────────────────────────────────
// convertTexture()
//──────────────────────────────────────────────────────────────
bool D8WFile::convertTexture(size_t pack, size_t idx, const std::string& outDds) const
{
    if (pack>=texBuf_.size() || idx>=texBuf_[pack].tex.size()) return false;
    const TextureTable& tbl = texBuf_[pack];
    const TextureHdr& hdr  = tbl.tex[idx];

    // compute offset in tBuf_
    size_t off = tbl.off;
    for(size_t k=0;k<idx;++k) off += tbl.tex[k].size;
    if (off + hdr.size > tBuf_.size()) return false;

    HANDLE h = CreateFileA(outDds.c_str(),GENERIC_WRITE,0,NULL,
                           CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    if (h==INVALID_HANDLE_VALUE) return false;

    bool ok = writeDDSHeader(h, hdr);
    if (ok) {
        DWORD bw;
        ok = WriteFile(h, &tBuf_[off], hdr.size, &bw, NULL) && (bw==hdr.size);
    }
    CloseHandle(h);
    return ok;
}

//──────────────────────────────────────────────────────────────
// convertTextureSet()
//──────────────────────────────────────────────────────────────
bool D8WFile::convertTextureSet(size_t pack, const std::string& dir) const
{
    if (pack>=texBuf_.size()) return false;
    CreateDirectoryA(dir.c_str(), NULL);

    char buf[260];
    size_t n = texBuf_[pack].tex.size();
    for(size_t i=0;i<n;++i) {
        sprintf_s(buf, "%s\\Tex%d%04d.dds", dir.c_str(), (int)pack, (int)i);
        if (!convertTexture(pack,i,buf))
            return false;
    }
    return true;
}




//──────────────────────── importTexture  (single .ddt) ───────────────────────
bool D8WFile::importTexture(size_t pack, size_t idx,
                            const std::string& inFile)
{
    if (pack >= texBuf_.size() || idx >= texBuf_[pack].tex.size())
        return false;

    /* read .ddt ----------------------------------------------------------------
       .ddt = 48-byte header (TextureHdr minus the first uint32 “size”)
              + raw image payload                                               */
    std::vector<BYTE> tmp;
    if (!loadFileToMem(inFile, tmp) ||
        tmp.size() < sizeof(TextureHdr) - 4)
        return false;

    /* reconstruct header (pad the missing size later) */
    TextureHdrEx newHdr;
    memset(&newHdr, 0, sizeof(newHdr));
    memcpy(((BYTE*)&newHdr) + 4, &tmp[0], sizeof(TextureHdr) - 4);

    size_t body = tmp.size() - (sizeof(TextureHdr) - 4);

    TextureTable& tbl = texBuf_[pack];
    TextureHdrEx& oldHdr = tbl.tex[idx];

    /* refuse if new payload is larger than original (protect shared .d8t) */
    if (body > oldHdr.size) return false;

    /* locate payload slot inside .d8t */
    size_t off = tbl.off;
    for (size_t k = 0; k < idx; ++k) off += tbl.tex[k].size;

    /* copy new payload;
       if smaller, trailing bytes from previous image remain unchanged */
    memcpy(&tBuf_[off], &tmp[(sizeof(TextureHdr) - 4)], body);

    /* update metadata, keep original size so offsets for other .d8w remain valid */
    uint32_t keepSize = oldHdr.size;
    oldHdr = newHdr;
    oldHdr.size = keepSize;
    oldHdr.modified = true;
    dirty_ = true;
    return true;
}

//──────────────────────── importTextureSet  (folder of .ddt) ─────────────────
bool D8WFile::importTextureSet(size_t pack, const std::string& dir)
{
    if (pack >= texBuf_.size()) return false;

    /* gather *.ddt filenames -------------------------------------------------- */
    std::vector<std::string> files;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "\\*.ddt").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE)
    {
        do { files.push_back(fd.cFileName); }
        while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    if (files.empty()) return false;

    std::sort(files.begin(), files.end());     // Tex0000.ddt … TexNNNN.ddt

    size_t texCnt = texBuf_[pack].tex.size();
    size_t num    = files.size() < texCnt ? files.size() : texCnt;

    char full[260];
    for (size_t i = 0; i < num; ++i)
    {
        sprintf_s(full, "%s\\%s", dir.c_str(), files[i].c_str());

        std::vector<BYTE> tmp;
        if (!loadFileToMem(full, tmp) ||
            tmp.size() < sizeof(TextureHdr) - 4)
            continue;                                   // skip malformed

        TextureHdrEx newHdr;
        memset(&newHdr, 0, sizeof(newHdr));
        memcpy(((BYTE*)&newHdr) + 4, &tmp[0], sizeof(TextureHdr) - 4);

        size_t body = tmp.size() - (sizeof(TextureHdr) - 4);
        TextureTable& tbl = texBuf_[pack];
        TextureHdrEx& oldHdr = tbl.tex[i];

        if (body > oldHdr.size) continue;               // oversized → skip

        /* compute payload offset */
        size_t off = tbl.off;
        for (size_t k = 0; k < i; ++k) off += tbl.tex[k].size;

        memcpy(&tBuf_[off], &tmp[(sizeof(TextureHdr) - 4)], body);

        uint32_t keepSize = oldHdr.size;
        oldHdr = newHdr;
        oldHdr.size = keepSize;
        oldHdr.modified = true;
        dirty_ = true;
    }
    return true;
}


bool D8WFile::isTextureModified(size_t p,size_t i) const
{
    return (p<texBuf_.size() && i<texBuf_[p].tex.size())
           ? texBuf_[p].tex[i].modified : false;
}
