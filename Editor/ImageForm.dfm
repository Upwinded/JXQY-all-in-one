object ImageEditor: TImageEditor
  Left = 0
  Top = 0
  Caption = 'Image Editor'
  ClientHeight = 515
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
    Width = 957
    Height = 158
    Margins.Left = 4
    Margins.Top = 4
    Margins.Right = 4
    Margins.Bottom = 4
    Align = alTop
    TabOrder = 0
    ExplicitWidth = 667
    object Panel3: TPanel
      Left = 1
      Top = 1
      Width = 528
      Height = 156
      Align = alLeft
      BevelOuter = bvNone
      TabOrder = 0
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
      object FileNameEdit: TEdit
        Left = 31
        Top = 46
        Width = 474
        Height = 25
        Margins.Left = 4
        Margins.Top = 4
        Margins.Right = 4
        Margins.Bottom = 4
        TabOrder = 0
      end
      object OpenBtn: TButton
        Left = 31
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
        Left = 407
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
    end
    object Panel4: TPanel
      Left = 529
      Top = 1
      Width = 427
      Height = 156
      Align = alClient
      BevelOuter = bvNone
      TabOrder = 1
      ExplicitLeft = 616
      ExplicitTop = 40
      ExplicitWidth = 185
      ExplicitHeight = 41
      object DirectionLabel: TLabel
        Left = 29
        Top = 37
        Width = 42
        Height = 17
        Caption = #26041#21521#25968
      end
      object IntervalLabel: TLabel
        Left = 24
        Top = 99
        Width = 56
        Height = 17
        Caption = #26102#38388#38388#38548
      end
      object DirectionEdit: TEdit
        Left = 96
        Top = 34
        Width = 121
        Height = 25
        NumbersOnly = True
        TabOrder = 0
        Text = '0'
      end
      object IntervalEdit: TEdit
        Left = 96
        Top = 96
        Width = 121
        Height = 25
        NumbersOnly = True
        TabOrder = 1
        Text = '0'
      end
    end
  end
  object Panel2: TPanel
    Left = 0
    Top = 158
    Width = 935
    Height = 357
    Margins.Left = 4
    Margins.Top = 4
    Margins.Right = 4
    Margins.Bottom = 4
    Align = alClient
    TabOrder = 1
    ExplicitWidth = 645
    object Image: TImage
      Left = 1
      Top = 1
      Width = 933
      Height = 355
      Margins.Left = 4
      Margins.Top = 4
      Margins.Right = 4
      Margins.Bottom = 4
      Align = alClient
      OnMouseMove = ImageMouseMove
      OnMouseUp = ImageMouseUp
      ExplicitWidth = 642
    end
  end
  object ScrollBar: TScrollBar
    Left = 935
    Top = 158
    Width = 22
    Height = 357
    Margins.Left = 4
    Margins.Top = 4
    Margins.Right = 4
    Margins.Bottom = 4
    Align = alRight
    Kind = sbVertical
    PageSize = 0
    TabOrder = 2
    OnChange = ScrollBarChange
    ExplicitLeft = 645
  end
  object OpenDialog: TOpenDialog
    Options = [ofHideReadOnly, ofAllowMultiSelect, ofEnableSizing]
    Left = 472
  end
  object PopupMenu: TPopupMenu
    Left = 408
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
end
