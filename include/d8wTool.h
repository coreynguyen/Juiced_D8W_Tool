#ifndef D8W_TOOL_GUI_H_
#define D8W_TOOL_GUI_H_

#include <wx/wx.h>
#include <wx/splitter.h>
#include <wx/treectrl.h>
#include <wx/statbmp.h>

#include "d8w_parser.h"
#include "DDSImage.h"

/* Tree payload ─────────────────────────────────────────────── */
struct TexItemData : public wxTreeItemData
{
    int bank;   // -2 = root .d8t  ,  -1 = .d8w  ,  -2/-1 combo unused
    int pack;   // -1 for .d8w nodes
    int tex;    // -1 for pack nodes
    TexItemData(int b=-2,int p=-1,int t=-1):bank(b),pack(p),tex(t){}
};

/* Application bootstrap ────────────────────────────────────── */
class d8wToolApp : public wxApp
{
public:  bool OnInit() override;
         ~d8wToolApp() override {}
};

/* Main window ──────────────────────────────────────────────── */
class MainFrame : public wxFrame
{
public:
    explicit MainFrame(const wxString& title);

private:                     /* widgets */
    wxSplitterWindow* splitter_;
    wxTreeCtrl*       tree_;
    wxPanel*          preview_;
    wxStaticBitmap*   thumb_;
    wxStaticText*     infoText_;

                             /* data */
    std::vector<juiced::D8WBank> banks_;   // one per discovered .d8w
    std::vector<wxString>        wNames_;  // filenames (for tree label)
    std::vector<BYTE>            bigT_;    // shared .d8t buffer
    wxString                     bigTPath_;

                             /* preview */
    wxBitmap rawBmp_;
    int      zoomPct_;
    bool     showAlpha_;
    enum { kZoomStep=25,kZoomMin=25,kZoomMax=800 };

                             /* helpers */
    void buildMenus();
    void buildAccelerators();

    /* menu handlers */
    void OnOpen        (wxCommandEvent&);
    void OnSave        (wxCommandEvent&);
    void OnExit        (wxCommandEvent&);

    void OnExport      (wxCommandEvent&);
    void OnConvert     (wxCommandEvent&);
    void OnImport      (wxCommandEvent&);

    void OnZoomIn      (wxCommandEvent&);
    void OnZoomOut     (wxCommandEvent&);
    void OnToggleAlpha (wxCommandEvent&);

    void OnAbout       (wxCommandEvent&);

    /* tree handlers */
    void OnSelChanged  (wxTreeEvent&);
    void OnTreeRClick  (wxTreeEvent&);

    /* misc */
    void clearTree();
    void populateTree();
    bool getSelection(int& bank,int& pack,int& tex) const;

    void showWInfo (int bank);
    void showPackInfo (int bank,int pack);
    void showTexInfo  (int bank,int pack,int tex);

    void applyZoom();
    void updateTitle();

    /* command IDs */
    enum { ID_Tree = wxID_HIGHEST+1,
           ID_Export, ID_Convert, ID_Import,
           ID_ZoomIn, ID_ZoomOut, ID_ToggleAlpha };

    wxDECLARE_EVENT_TABLE();
};

#endif /* D8W_TOOL_GUI_H_ */
