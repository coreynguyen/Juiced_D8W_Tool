#ifndef D8W_TOOL_GUI_H_
#define D8W_TOOL_GUI_H_

#include <wx/wx.h>
#include <wx/splitter.h>
#include <wx/treectrl.h>
#include <wx/statbmp.h>
#include "d8w_parser.h"
#include "DDSImage.h"

/* -------------------------------------------------------------
   Tree-item payload
   -------------------------------------------------------------*/
struct TexItemData : public wxTreeItemData
{
    int pack;   // -2 root, -1 pack, >=0 texture
    int tex;
    TexItemData(int p=-2,int t=-1): pack(p), tex(t) {}
};

/* -------------------------------------------------------------
   App bootstrap
   -------------------------------------------------------------*/
class d8wToolApp : public wxApp
{
public:  bool OnInit() override;
         ~d8wToolApp() override = default;
};

/* -------------------------------------------------------------
   Main window
   -------------------------------------------------------------*/
class MainFrame : public wxFrame
{
public:
    explicit MainFrame(const wxString& title);

private:                                    /* widgets */
    wxSplitterWindow* splitter_;
    wxTreeCtrl*       tree_;
    wxPanel*          preview_;
    wxStaticBitmap*   thumb_;
    wxStaticText*     infoText_;

                                            /* data */
    juiced::D8WFile  bank_;
    wxString         curW_, curT_;
    wxBitmap         rawBmp_;               // original RGBA thumb
    int              zoomPct_;              // 25-800 %

    static const int kZoomStep = 25, kZoomMin = 25, kZoomMax = 800;

                                            /* build */
    void buildMenus();
    void buildAccelerators();

                                            /* commands */
    void OnOpen     (wxCommandEvent&);
    void OnSave     (wxCommandEvent&);
    void OnExit     (wxCommandEvent&);

    void OnExport   (wxCommandEvent&);
    void OnConvert  (wxCommandEvent&);
    void OnImport   (wxCommandEvent&);

    void OnZoomIn   (wxCommandEvent&);
    void OnZoomOut  (wxCommandEvent&);

    void OnAbout    (wxCommandEvent&);
    void OnSelChanged(wxTreeEvent&);
    void OnTreeRClick(wxTreeEvent&);          // NEW

                                            /* helpers */
    void clearTree();
    void populateTree();
    bool getSelection(int& pack,int& tex) const;

    void showPackInfo(int pack);
    void showTextureInfo(int pack,int tex);

    void applyZoom();
    void updateTitle();

                                            /* IDs */
    enum {
        ID_Tree = wxID_HIGHEST+1,
        ID_Export, ID_Convert, ID_Import,
        ID_ZoomIn, ID_ZoomOut
    };

    wxDECLARE_EVENT_TABLE();
};

#endif /* D8W_TOOL_GUI_H_ */
