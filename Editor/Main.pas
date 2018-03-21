unit MAIN;

interface

uses Windows, SysUtils, Classes, Graphics, Forms, Controls, Menus,
  StdCtrls, Dialogs, Buttons, Messages, ExtCtrls, ComCtrls, ToolWin, head, ImageForm;

type
  TSHFEditorMainForm = class(TForm)
    MainMenu1: TMainMenu;
    File1: TMenuItem;
    Window1: TMenuItem;
    Help1: TMenuItem;
    N1: TMenuItem;
    ExitItem: TMenuItem;
    HelpAboutItem: TMenuItem;
    Edit1: TMenuItem;
    N2: TMenuItem;
    TrayIcon1: TTrayIcon;
    PopupMenu1: TPopupMenu;
    About1: TMenuItem;
    N3: TMenuItem;
    Exit1: TMenuItem;
    N4: TMenuItem;
    ImageEditorBtn: TMenuItem;
    procedure HelpAbout1Execute(Sender: TObject);
    procedure ExitItemClick(Sender: TObject);
    procedure About1Click(Sender: TObject);
    procedure Exit1Click(Sender: TObject);
    procedure N4Click(Sender: TObject);
    procedure ImageEditorBtnClick(Sender: TObject);
  private
    { Private declarations }
  public
    { Public declarations }
  end;

var
  SHFEditorMainForm: TSHFEditorMainForm;

implementation

{$R *.dfm}

uses RePack, about;

procedure TSHFEditorMainForm.About1Click(Sender: TObject);
begin
  HelpAbout1Execute(sender);
end;

procedure TSHFEditorMainForm.Exit1Click(Sender: TObject);
begin
  ExitItemClick(sender);
end;

procedure TSHFEditorMainForm.ExitItemClick(Sender: TObject);
begin
  Halt;
end;

procedure TSHFEditorMainForm.HelpAbout1Execute(Sender: TObject);
begin
  AboutBox.ShowModal;
end;

procedure TSHFEditorMainForm.ImageEditorBtnClick(Sender: TObject);
var
  CImageEditorF: TImageEditor;
  I: integer;
begin
  if not CImageEditor then
  begin
    CImageEditorF := TImageEditor.Create(self);
    CImageEditor := true;
    CImageEditorH := CImageEditorF.Handle;
  end
  else
  begin
    for I := 0 to self.MDIChildCount - 1 do
      if self.MDIChildren[I].Handle = CImageEditorH then
        self.MDIChildren[I].Show;
  end;
end;

procedure TSHFEditorMainForm.N4Click(Sender: TObject);
var
  CRePackF: TRepackForm;
  I: integer;
begin
  if not CRePack then
  begin
    CRePackF := TRepackForm.Create(self);
    CRePack := true;
    CRePackH := CRePackF.Handle;
  end
  else
  begin
    for I := 0 to self.MDIChildCount - 1 do
      if self.MDIChildren[I].Handle = CRePackH then
        self.MDIChildren[I].Show;
  end;
end;

end.
