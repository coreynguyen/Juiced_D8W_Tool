#ifndef JUICED_D8W_PARSER_H_
#define JUICED_D8W_PARSER_H_

#include <windows.h>
#include <string>
#include <vector>
#include <stdint.h>

extern std::string gLastErr;



namespace juiced
{

template<typename T> T LEread (const BYTE*& p);
template<typename T> void LEwrite(BYTE*& p, T v);

#pragma pack(push,1)
struct TextureHdr
{
uint32_t size;
uint32_t type;
uint32_t width, height;
uint32_t mipCnt;
uint32_t unk07, unk08, unk09, unk10, unk11;
float unk12, unk13;
};
#pragma pack(pop)

struct TextureHdrEx : public TextureHdr
{
uint32_t fileOff;
bool modified;
};

class D8WBank;

struct Reference
{
TextureHdrEx* hdr;
D8WBank* ownerBank;

uint32_t* pSetSize;
uint32_t* pFileTotal;
};

struct TextureTable
{
uint32_t skip;
uint32_t size;
uint32_t absOff;

std::vector<TextureHdrEx> tex;
std::vector<Reference> refs;
};

struct TextureSet
{
std::string name;
std::vector<int32_t> indexTable;
};

typedef std::vector<BYTE> UnknownTailRaw;

class D8TFile
{
public:
bool load(const std::string& path);

const std::vector<BYTE>& buffer() const { return buf_; }
const std::string& path () const { return pathT_; }

private:
bool loadFileToMem(const std::string& p,std::vector<BYTE>& dst) const;

std::string pathT_;
std::vector<BYTE> buf_;
};

class D8WBank
{
public:
D8WBank(); ~D8WBank();
    /* allow GUI to fetch the last parser error */
    const std::string& lastError() const { return gLastErr; }

bool load(const std::string& d8wPath,
const std::vector<BYTE>& sharedTbuf);
bool save(const std::string& outW,const std::string& outT);

const std::string& d8wPath() const { return pathW_; }
const std::string& d8tPath() const { return pathT_; }

size_t texturePackCount() const { return texBuf_.size(); }
size_t textureCount(size_t p) const;
const TextureHdr& texture(size_t p,size_t i) const;

bool isTextureModified(size_t p,size_t i) const;
bool isDirty() const { return dirty_; }

bool exportTexture (size_t p,size_t i,const std::string& outDdt) const;
bool exportTextureSet (size_t p,const std::string& outDir) const;
bool convertTexture (size_t p,size_t i,const std::string& outDds) const;
bool convertTextureSet(size_t p,const std::string& outDir) const;
bool importTexture (size_t p,size_t i,const std::string& inFile);
bool importTextureSet (size_t p,const std::string& dir);

const UnknownTailRaw& tailData() const { return tailRaw_; }

std::vector<TextureTable>& tables() { return texBuf_; }
const std::vector<TextureTable>& tables() const { return texBuf_; }

std::vector<BYTE>* tBuffer() const { return tBuf_; }

private:

bool loadFileToMem(const std::string& p,std::vector<BYTE>& dst) const;
bool locateD8T(const std::string& folder,const std::string& stem,
const std::string& hint,std::string& out) const;

bool dirty_;
bool headerFixed;

std::string pathW_, pathT_;

std::vector<BYTE> wBuf_;
std::vector<BYTE>* tBuf_;

std::vector<TextureTable> texBuf_;
std::vector<TextureSet> texSet_;
UnknownTailRaw tailRaw_;
};

}
#endif
