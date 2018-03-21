object ImageEditor: TImageEditor
  Left = 0
  Top = 0
  Caption = 'Image Editor'
  ClientHeight = 394
  ClientWidth = 510
  Color = clBtnFace
  DoubleBuffered = True
  Font.Charset = DEFAULT_CHARSET
  Font.Color = clWindowText
  Font.Height = -11
  Font.Name = 'Tahoma'
  Font.Style = []
  FormStyle = fsMDIChild
  OldCreateOrder = False
  Visible = True
  WindowState = wsMaximized
  OnClose = FormClose
  OnCreate = FormCreate
  OnResize = FormResize
  PixelsPerInch = 96
  TextHeight = 13
  object Panel1: TPanel
    Left = 0
    Top = 0
    Width = 510
    Height = 121
    Align = alTop
    TabOrder = 0
    object FileNameLabel: TLabel
      Left = 24
      Top = 16
      Width = 36
      Height = 13
      Caption = #25991#20214#21517
    end
    object FileNameEdit: TEdit
      Left = 24
      Top = 35
      Width = 433
      Height = 21
      TabOrder = 0
    end
    object OpenBtn: TButton
      Left = 24
      Top = 72
      Width = 75
      Height = 25
      Caption = #25171#24320
      TabOrder = 1
      OnClick = OpenBtnClick
    end
  end
  object Panel2: TPanel
    Left = 0
    Top = 121
    Width = 493
    Height = 273
    Align = alClient
    TabOrder = 1
    object Image: TImage
      Left = 1
      Top = 1
      Width = 491
      Height = 271
      Align = alClient
      OnMouseMove = ImageMouseMove
      ExplicitLeft = 128
      ExplicitTop = 112
      ExplicitWidth = 105
      ExplicitHeight = 105
    end
  end
  object ScrollBar: TScrollBar
    Left = 493
    Top = 121
    Width = 17
    Height = 273
    Align = alRight
    Kind = sbVertical
    PageSize = 0
    TabOrder = 2
    OnChange = ScrollBarChange
  end
  object OpenDialog: TOpenDialog
    Left = 472
  end
end
