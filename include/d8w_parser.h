#ifndef JUICED_D8W_PARSER_H_
#define JUICED_D8W_PARSER_H_

#include <windows.h>
#include <string>
#include <vector>
#include <stdint.h>

namespace juiced
{
/* ─── little-endian helpers (used only in .cpp) ───────────── */
template<typename T> T  LEread (const BYTE*& p);
template<typename T> void LEwrite(BYTE*& p, T v);

/* ─── on-disk header (48 bytes) ───────────────────────────── */
#pragma pack(push,1)
struct TextureHdr
{
    uint32_t size;      /* bytes in .d8t                       */
    uint32_t type;      /* FourCC (DXT1 / DXT5 / 0x15 …)       */
    uint32_t width;
    uint32_t height;
    uint32_t mipCnt;
    uint32_t unk07, unk08, unk09, unk10, unk11;
    float    unk12, unk13;
};
#pragma pack(pop)

/* ─── runtime extension (not written back to file) ───────── */
struct TextureHdrEx : public TextureHdr
{
    uint32_t fileOff;   /* absolute offset inside .d8t buffer  */
    bool     modified;
};

/* one texture-buffer (= “pack” in UI) ---------------------- */
struct TextureTable
{
    uint32_t skip;      /* #bytes to skip BEFORE first image   */
    uint32_t size;      /* total size of this pack in .d8t     */
    uint32_t absOff;    /* absolute start address in .d8t      */
    std::vector<TextureHdrEx> tex;
};

/* name + index array -------------------------------------- */
struct TextureSet
{
    std::string           name;
    std::vector<int32_t>  indexTable;
};

typedef std::vector<BYTE> UnknownTailRaw;

/*────────────────────────  .d8t owner  ─────────────────────*/
class D8TFile
{
public:
    bool load(const std::string& path);

    const std::vector<BYTE>& buffer() const { return buf_; }
    const std::string&       path  () const { return pathT_; }

private:
    bool loadFileToMem(const std::string& p,std::vector<BYTE>& dst) const;

    std::string         pathT_;
    std::vector<BYTE>   buf_;
};

/*────────────────────────  .d8w bank  ─────────────────────*/
class D8WBank
{
public:
    D8WBank();  ~D8WBank();

    bool load(const std::string& d8wPath,
              const std::vector<BYTE>& sharedTbuf);
    bool save(const std::string& outW,const std::string& outT);

    const std::string& d8wPath() const { return pathW_; }
    const std::string& d8tPath() const { return pathT_; }

    /* --- queries -------------------------------------------------- */
    size_t texturePackCount()               const { return texBuf_.size(); }
    size_t textureCount(size_t p)           const;
    const TextureHdr& texture(size_t p,size_t i) const;

    bool  isTextureModified(size_t p,size_t i) const;
    bool  isDirty() const { return dirty_; }

    /* --- export / convert / import -------------------------------- */
    bool exportTexture     (size_t p,size_t i,const std::string& outDdt) const;
    bool exportTextureSet  (size_t p,const std::string& outDir)          const;
    bool convertTexture    (size_t p,size_t i,const std::string& outDds) const;
    bool convertTextureSet (size_t p,const std::string& outDir)          const;
    bool importTexture     (size_t p,size_t i,const std::string& inDdt);
    bool importTextureSet  (size_t p,const std::string& dir);

    /* raw tail (for research) */
    const UnknownTailRaw& tailData() const { return tailRaw_; }

private:
    /* helpers (only .cpp needs them) */
    bool loadFileToMem(const std::string& p,std::vector<BYTE>& dst) const;
    bool locateD8T(const std::string& folder,const std::string& stem,
                   const std::string& hint,std::string& out) const;

    /* state -------------------------------------------------------- */
    bool                dirty_;
    std::string         pathW_, pathT_;

    std::vector<BYTE>         wBuf_;   /* local copy of .d8w          */
    std::vector<BYTE>*        tBuf_;   /* *** borrowed from D8TFile *** */

    std::vector<TextureTable> texBuf_;
    std::vector<TextureSet>   texSet_;
    UnknownTailRaw            tailRaw_;
};

} // namespace juiced
#endif /* JUICED_D8W_PARSER_H_ */
