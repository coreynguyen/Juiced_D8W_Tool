/****************************************************************************************
 *  @file    d8w_parser.h
 *  @brief   Stand-alone loader / editor for Juiced texture banks (.d8w + .d8t)
 *  @author  <your-name>
 *
 *  • Pre-C++11, Win-32 friendly (only <windows.h>, <string>, <vector>, <stdint.h>)
 *  • No external dependencies; GUI (wxWidgets, ImGui, etc.) can link directly
 *  • Companion implementation: **d8w_parser.cpp**
 *
 *  DATA LAYOUT
 *  ───────────
 *      ┌───────────────────┐      .d8w
 *      │ file header       │  12 B
 *      │ texture tables    │  N×(12 B + 52 B×K)
 *      │ texture-set block │  variable
 *      │ mystery tail      │  raw bytes (kept verbatim)
 *      └───────────────────┘
 *
 *      ┌───────────────────┐      .d8t  (shared by many .d8w)
 *      │ block #0 payload  │
 *      │ block #1 payload  │
 *      │ …                 │
 *      └───────────────────┘
 *
 *  DESIGN NOTES
 *  ────────────
 *  •  **TextureHdrEx::modified** lets a GUI tint edited entries.
 *  •  **dirty_** goes true on any import; cleared after save().
 *  •  Bulk import refuses files whose payload is larger than the
 *     original, keeping shared .d8t offsets stable.
 *
 *  Typical call-flow from UI:
 *
 *      juiced::D8WFile bank;
 *      bank.load("track.d8w");
 *      bank.importTextureSet(0, "EditedPack");
 *      if(bank.isDirty()) bank.save("track.d8w", "track.d8t");
 *
 ***************************************************************************************/
#ifndef JUICED_D8W_PARSER_H_
#define JUICED_D8W_PARSER_H_

/*------------------------------------------------------------------------------
    Standard / Win-32 includes
------------------------------------------------------------------------------*/
#include <windows.h>                 //!< Win32 file-API (CreateFile, ReadFile…)
#include <string>                    //!< std::string
#include <vector>                    //!< std::vector
#include <stdint.h>                  //!< fixed-width integers


/*------------------------------------------------------------------------------
    juiced namespace – all public symbols live here
------------------------------------------------------------------------------*/
namespace juiced {

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    Little-endian helpers  (defined in d8w_parser.cpp)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
/**
 * @brief  Read little-endian value and advance pointer.
 * @tparam T Plain-old-data integral or float.
 * @param   p  Reference to current cursor.
 * @return     Value of type @p T.
 */
template<typename T> T   LEread (const BYTE* &p);

/**
 * @brief  Write little-endian value and advance pointer.
 * @tparam T Plain-old-data integral or float.
 * @param   p  Reference to current cursor.
 * @param   v  Value to write.
 */
template<typename T> void LEwrite(BYTE* &p, T v);

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    TextureHdr  –  52-byte on-disk header describing one image
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
#pragma pack(push,1)
/**
 * @struct TextureHdr
 * @brief  Exact 52-byte structure stored in a .d8w entry.
 *
 * The first field (`size`) is *not* emitted when exporting a
 * standalone `.ddt` file (tool uses only 48 bytes).
 */
struct TextureHdr
{
    uint32_t size;     //!< Payload length in .d8t (bytes)
    uint32_t type;     //!< FourCC  ('DXT1','DXT5',0x00000015 for ARGB8888…)
    uint32_t width;    //!< Pixels
    uint32_t height;   //!< Pixels
    uint32_t mipCnt;   //!< Mip-map levels
    uint32_t unk07;    //!< Always 2 in Juiced
    uint32_t unk08;    //!< Always 2
    uint32_t unk09;    //!< Always 2
    uint32_t unk10;    //!< Always 1
    uint32_t unk11;    //!< Always 1
    float    unk12;    //!< Often 1.5
    float    unk13;    //!< Often 0.0
};
#pragma pack(pop)

/**
 * @struct TextureHdrEx
 * @brief  In-RAM header with extra @ref modified flag for UI.
 */
struct TextureHdrEx : public TextureHdr
{
    bool modified;     //!< True once body or metadata replaced after load()
};

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    TextureTable  –  one “texture pack” / data block inside .d8t
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
struct TextureTable
{
    uint32_t                 off;   //!< Byte offset into .d8t
    uint32_t                 size;  //!< Total length of this block
    std::vector<TextureHdrEx> tex;  //!< Images inside the block
};

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    TextureSet  –  32-byte name + indirection indices
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
struct TextureSet
{
    std::string          name;        //!< 0-terminated string from file
    std::vector<int32_t> indexTable;  //!< Signed indices, variable stride
};

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    Raw mystery tail  –  unparsed bytes after texture-set section
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
typedef std::vector<BYTE> UnknownTailRaw;

/*==============================================================================
    class D8WFile
==============================================================================*/
/**
 * @class D8WFile
 * @brief  In-memory representation of a .d8w file and its companion .d8t.
 *
 * Provides high-level operations for loading, exporting / importing textures,
 * tracking edits, and finally saving both files back to disk.
 */
class D8WFile
{
public:
    /** Construct with `dirty_ = false`. */
    D8WFile();
    ~D8WFile();

    /*------------------------------------------------------------------------*/
    /** @name Core I/O                                                        */
    /*------------------------------------------------------------------------*/
    /** @{ */
    /**
     * @brief Load .d8w and matching .d8t into memory.
     *
     * @param d8wPath  Path to .d8w file.
     * @param d8tHint  Optional explicit .d8t; otherwise auto-detect.
     * @return         true on success.
     */
    bool load(const std::string& d8wPath, const std::string& d8tHint = "");
    const std::string& d8tPath() const { return pathT_; }   // ← NEW accessor

    /**
     * @brief Save current state back to disk (rewrites both files).
     *
     * @param d8wOut  Output path for new .d8w
     * @param d8tOut  Output path for new .d8t
     * @return        true on success; false if !dirty_ or write fails.
     */
    bool save(const std::string& d8wOut, const std::string& d8tOut);
    /** @} */

    /*------------------------------------------------------------------------*/
    /** @name Read-only queries                                               */
    /*------------------------------------------------------------------------*/
    /** @{ */
    size_t texturePackCount()                const; //!< packs inside file
    size_t textureCount(size_t pack)         const; //!< textures in pack
    const TextureHdr& texture(size_t pack,size_t idx) const; //!< const ref

    bool   isTextureModified(size_t pack,size_t idx) const;   //!< per-image
    bool   isDirty() const                 { return dirty_; } //!< any change

    /**
     * @brief  Raw bytes after texture-set section (for hex view).
     */
    const UnknownTailRaw& tailData() const { return tailRaw_; }
    /** @} */

    /*------------------------------------------------------------------------*/
    /** @name Export helpers                                                  */
    /*------------------------------------------------------------------------*/
    /** @{ */
    /**
     * @brief Export a single texture to 48-byte-header `.ddt`.
     *
     * @param pack    Texture pack index.
     * @param idx     Texture index inside pack.
     * @param outDdt  Destination file path.
     * @return        true on success.
     */
    bool exportTexture(size_t pack,size_t idx,
                       const std::string& outDdt) const;

    /**
     * @brief Dump every texture from a pack into a folder.
     *
     * Filenames follow `Tex<pack><index>.ddt` (zero-padded).
     */
    bool exportTextureSet(size_t pack,const std::string& outDir) const;
    /** @} */


bool convertTexture   (size_t pack,size_t idx,const std::string& outDds) const;
bool convertTextureSet(size_t pack,const std::string& outDir)    const;


    /*------------------------------------------------------------------------*/
    /** @name Import helpers                                                  */
    /*------------------------------------------------------------------------*/
    /** @{ */
    /**
     * @brief Replace one texture from a `.ddt` file.
     *
     * Allowed only if new payload byte length ≤ original length.
     */
    bool importTexture(size_t pack,size_t idx,
                       const std::string& inDdt);

    /**
     * @brief Bulk replace an entire pack from folder of `.ddt`s.
     *
     * Files are loaded in lexicographic order; each must fit into the
     * original slot (no payload enlargement).
     */
    bool importTextureSet(size_t pack,const std::string& dir);
    /** @} */



private:
    /*------------------------------------------------------------------------*/
    /** @name Internal helpers (defined in .cpp)                              */
    /*------------------------------------------------------------------------*/
    /** @{ */
    bool  loadFileToMem(const std::string& path,
                        std::vector<BYTE>& buf) const;

    bool  locateD8T(const std::string& folder,const std::string& stem,
                    const std::string& hint,   std::string& outPath) const;
    /** @} */

    /*------------------------------------------------------------------------*/
    /** @name In-memory state                                                 */
    /*------------------------------------------------------------------------*/
    bool                       dirty_;      //!< true once something imported

    std::string                pathW_;      //!< original .d8w path
    std::string                pathT_;      //!< original .d8t path

    std::vector<BYTE>          wBuf_;       //!< original .d8w blob
    std::vector<BYTE>          tBuf_;       //!< current .d8t blob

    std::vector<TextureTable>  texBuf_;     //!< all texture packs
    std::vector<TextureSet>    texSet_;     //!< optional indirection
    UnknownTailRaw             tailRaw_;    //!< copy of unknown trailing bytes
};

} /* namespace juiced */
#endif /* JUICED_D8W_PARSER_H_ */
