object StarterForm: TStarterForm
  Left = 0
  Top = 0
  BorderIcons = [biSystemMenu]
  Caption = 'Starter of Sword Heroes'#39' Fate'
  ClientHeight = 271
  ClientWidth = 399
  Color = clBtnFace
  Constraints.MaxHeight = 310
  Constraints.MaxWidth = 415
  Constraints.MinHeight = 310
  Constraints.MinWidth = 415
  DoubleBuffered = True
  Font.Charset = DEFAULT_CHARSET
  Font.Color = clWindowText
  Font.Height = -11
  Font.Name = 'Tahoma'
  Font.Style = []
  OldCreateOrder = False
  Position = poDesktopCenter
  OnCreate = FormCreate
  PixelsPerInch = 96
  TextHeight = 13
  object MusicLabel: TLabel
    Left = 24
    Top = 21
    Width = 51
    Height = 13
    Caption = 'MusicLabel'
  end
  object SoundLabel: TLabel
    Left = 24
    Top = 77
    Width = 55
    Height = 13
    Caption = 'SoundLabel'
  end
  object MusicScrollbar: TScrollBar
    Left = 24
    Top = 40
    Width = 352
    Height = 17
    PageSize = 0
    TabOrder = 5
    OnChange = MusicScrollbarChange
  end
  object SoundScrollbar: TScrollBar
    Left = 24
    Top = 96
    Width = 352
    Height = 17
    PageSize = 0
    TabOrder = 6
    OnChange = SoundScrollbarChange
  end
  object FullScreenCheckBox: TCheckBox
    Left = 24
    Top = 136
    Width = 97
    Height = 17
    Caption = #26159#21542#20840#23631
    TabOrder = 7
  end
  object AlphaCheckBox: TCheckBox
    Left = 247
    Top = 136
    Width = 129
    Height = 17
    Caption = #20027#35282#36974#25377#37096#20998#21322#36879#26126
    TabOrder = 8
  end
  object StartBtn: TButton
    Left = 24
    Top = 165
    Width = 113
    Height = 37
    Caption = #21551#21160#28216#25103
    TabOrder = 0
    OnClick = StartBtnClick
  end
  object SaveBtn: TButton
    Left = 143
    Top = 165
    Width = 65
    Height = 87
    Caption = #20445#23384#35774#32622
    TabOrder = 2
    OnClick = SaveBtnClick
  end
  object CmdStartBtn: TButton
    Left = 24
    Top = 215
    Width = 113
    Height = 37
    Caption = 'CMD'#27169#24335#21551#21160
    TabOrder = 1
    OnClick = CmdStartBtnClick
  end
  object EnvBtn: TButton
    Left = 295
    Top = 165
    Width = 81
    Height = 87
    Caption = #23433#35013#36816#34892#29615#22659
    TabOrder = 3
    OnClick = EnvBtnClick
  end
  object EditorBtn: TButton
    Left = 215
    Top = 165
    Width = 74
    Height = 87
    Caption = #21551#21160#32534#36753#22120
    TabOrder = 4
    OnClick = EditorBtnClick
  end
end
