object ImageEditor: TImageEditor
  Left = 0
  Top = 0
  Caption = 'Image Editor'
  ClientHeight = 633
  ClientWidth = 957
  Color = clBtnFace
  DoubleBuffered = True
  Font.Charset = DEFAULT_CHARSET
  Font.Color = clWindowText
  Font.Height = -14
  Font.Name = 'Tahoma'
  Font.Style = []
  FormStyle = fsMDIChild
  OldCreateOrder = False
  Visible = True
  WindowState = wsMaximized
  OnClose = FormClose
  OnCreate = FormCreate
  OnResize = FormResize
  PixelsPerInch = 120
  TextHeight = 17
  object Panel1: TPanel
    Left = 0
    Top = 0
    Width = 310
    Height = 633
    Margins.Left = 4
    Margins.Top = 4
    Margins.Right = 4
    Margins.Bottom = 4
    Align = alLeft
    TabOrder = 0
    ExplicitHeight = 515
    object FileNameLabel: TLabel
      Left = 31
      Top = 21
      Width = 42
      Height = 17
      Margins.Left = 4
      Margins.Top = 4
      Margins.Right = 4
      Margins.Bottom = 4
      Caption = #25991#20214#21517
    end
    object IntervalLabel: TLabel
      Left = 154
      Top = 149
      Width = 56
      Height = 17
      Caption = #26102#38388#38388#38548
    end
    object DirectionLabel: TLabel
      Left = 24
      Top = 149
      Width = 42
      Height = 17
      Caption = #26041#21521#25968
    end
    object FileNameEdit: TEdit
      Left = 24
      Top = 46
      Width = 249
      Height = 25
      Margins.Left = 4
      Margins.Top = 4
      Margins.Right = 4
      Margins.Bottom = 4
      ImeName = #24494#36719#25340#38899#36755#20837#27861' 2010'
      TabOrder = 0
    end
    object OpenBtn: TButton
      Left = 24
      Top = 94
      Width = 98
      Height = 33
      Margins.Left = 4
      Margins.Top = 4
      Margins.Right = 4
      Margins.Bottom = 4
      Caption = #25171#24320
      TabOrder = 1
      OnClick = OpenBtnClick
    end
    object SaveBtn: TButton
      Left = 175
      Top = 94
      Width = 98
      Height = 33
      Margins.Left = 4
      Margins.Top = 4
      Margins.Right = 4
      Margins.Bottom = 4
      Caption = #20445#23384
      TabOrder = 2
      OnClick = SaveBtnClick
    end
    object DirectionEdit: TEdit
      Left = 72
      Top = 146
      Width = 57
      Height = 25
      ImeName = #24494#36719#25340#38899#36755#20837#27861' 2010'
      NumbersOnly = True
      TabOrder = 3
      Text = '0'
      OnChange = DirectionEditChange
    end
    object IntervalEdit: TEdit
      Left = 216
      Top = 146
      Width = 57
      Height = 25
      ImeName = #24494#36719#25340#38899#36755#20837#27861' 2010'
      NumbersOnly = True
      TabOrder = 4
      Text = '0'
      OnChange = IntervalEditChange
    end
    object Panel3: TPanel
      Left = 2
      Top = 270
      Width = 302
      Height = 302
      BevelOuter = bvLowered
      TabOrder = 5
      object DynamicImage: TImage
        Left = 1
        Top = 1
        Width = 300
        Height = 300
        Align = alClient
        ExplicitTop = 0
      end
    end
    object DynamicCheckBox: TCheckBox
      Left = 105
      Top = 232
      Width = 97
      Height = 17
      Caption = #21160#30011#25928#26524
      TabOrder = 6
      OnClick = DynamicCheckBoxClick
    end
  end
  object Panel2: TPanel
    Left = 310
    Top = 0
    Width = 625
    Height = 633
    Margins.Left = 4
    Margins.Top = 4
    Margins.Right = 4
    Margins.Bottom = 4
    Align = alClient
    TabOrder = 1
    ExplicitLeft = 0
    ExplicitTop = 158
    ExplicitWidth = 935
    ExplicitHeight = 357
    object Image: TImage
      Left = 1
      Top = 1
      Width = 623
      Height = 631
      Margins.Left = 4
      Margins.Top = 4
      Margins.Right = 4
      Margins.Bottom = 4
      Align = alClient
      OnMouseMove = ImageMouseMove
      OnMouseUp = ImageMouseUp
      ExplicitWidth = 642
      ExplicitHeight = 355
    end
  end
  object ScrollBar: TScrollBar
    Left = 935
    Top = 0
    Width = 22
    Height = 633
    Margins.Left = 4
    Margins.Top = 4
    Margins.Right = 4
    Margins.Bottom = 4
    Align = alRight
    Kind = sbVertical
    PageSize = 0
    TabOrder = 2
    OnChange = ScrollBarChange
    ExplicitTop = 158
    ExplicitHeight = 357
  end
  object OpenDialog: TOpenDialog
    Options = [ofHideReadOnly, ofAllowMultiSelect, ofEnableSizing]
    Left = 144
  end
  object PopupMenu: TPopupMenu
    Left = 184
    object EditItem: TMenuItem
      Caption = #32534#36753#22270#29255
      OnClick = EditItemClick
    end
    object DeleteItem: TMenuItem
      Caption = #21024#38500#22270#29255
      OnClick = DeleteItemClick
    end
    object InsertItem: TMenuItem
      Caption = #25554#20837#22270#29255
      OnClick = InsertItemClick
    end
    object AddItem: TMenuItem
      Caption = #28155#21152#22270#29255#21040#26368#21518
      OnClick = AddItemClick
    end
  end
  object Timer: TTimer
    Enabled = False
    OnTimer = TimerTimer
    Left = 96
  end
end
