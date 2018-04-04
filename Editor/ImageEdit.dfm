object ImageEditForm: TImageEditForm
  Left = 0
  Top = 0
  Caption = 'ImageEditForm'
  ClientHeight = 397
  ClientWidth = 641
  Color = clBtnFace
  Font.Charset = DEFAULT_CHARSET
  Font.Color = clWindowText
  Font.Height = -13
  Font.Name = 'Tahoma'
  Font.Style = []
  OldCreateOrder = False
  Position = poMainFormCenter
  OnClose = FormClose
  OnCreate = FormCreate
  PixelsPerInch = 120
  TextHeight = 16
  object LeftPanel: TPanel
    Left = 0
    Top = 0
    Width = 201
    Height = 397
    Align = alLeft
    TabOrder = 0
    object XLabel: TLabel
      Left = 40
      Top = 18
      Width = 36
      Height = 16
      Caption = 'x'#20559#31227
    end
    object YLabel: TLabel
      Left = 40
      Top = 70
      Width = 36
      Height = 16
      Caption = 'y'#20559#31227
    end
    object XEdit: TEdit
      Left = 40
      Top = 40
      Width = 121
      Height = 24
      TabOrder = 4
      Text = '0'
      OnKeyPress = XEditKeyPress
    end
    object YEdit: TEdit
      Left = 40
      Top = 92
      Width = 121
      Height = 24
      TabOrder = 5
      Text = '0'
      OnKeyPress = YEditKeyPress
    end
    object OkBtn: TButton
      Left = 40
      Top = 194
      Width = 121
      Height = 41
      Caption = #20445#23384
      ModalResult = 1
      TabOrder = 2
      OnClick = OkBtnClick
    end
    object CancelBtn: TButton
      Left = 40
      Top = 257
      Width = 121
      Height = 41
      Caption = #21462#28040
      ModalResult = 2
      TabOrder = 3
    end
    object ReplaceBtn: TButton
      Left = 111
      Top = 132
      Width = 74
      Height = 41
      Caption = #26367#25442#22270#29255
      TabOrder = 1
      OnClick = ReplaceBtnClick
    end
    object OffsetBtn: TButton
      Left = 16
      Top = 132
      Width = 73
      Height = 41
      Caption = #20445#23384#20559#31227
      TabOrder = 0
      OnClick = OffsetBtnClick
    end
  end
  object ClientPanel: TPanel
    Left = 201
    Top = 0
    Width = 440
    Height = 397
    Align = alClient
    BevelOuter = bvLowered
    TabOrder = 1
    ExplicitLeft = 256
    ExplicitTop = 178
    ExplicitWidth = 185
    ExplicitHeight = 41
    object ScrollBox: TScrollBox
      Left = 1
      Top = 1
      Width = 438
      Height = 395
      Align = alClient
      TabOrder = 0
      ExplicitWidth = 345
      object Image: TImage
        Left = 0
        Top = 0
        Width = 600
        Height = 600
        OnMouseDown = ImageMouseDown
      end
    end
  end
  object OpenDialog: TOpenDialog
    Left = 8
    Top = 336
  end
end
