object StarterForm: TStarterForm
  Left = 0
  Top = 0
  BorderIcons = [biSystemMenu]
  Caption = 'Starter of Sword Heroes'#39' Fate'
  ClientHeight = 321
  ClientWidth = 416
  Color = clBtnFace
  Constraints.MaxHeight = 360
  Constraints.MaxWidth = 432
  Constraints.MinHeight = 360
  Constraints.MinWidth = 432
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
    Left = 25
    Top = 22
    Width = 51
    Height = 13
    Caption = 'MusicLabel'
  end
  object SoundLabel: TLabel
    Left = 25
    Top = 81
    Width = 55
    Height = 13
    Caption = 'SoundLabel'
  end
  object DisplayLabel: TLabel
    Left = 25
    Top = 143
    Width = 60
    Height = 13
    Margins.Left = 2
    Margins.Top = 2
    Margins.Right = 2
    Margins.Bottom = 2
    Caption = #28216#25103#20998#36776#29575
  end
  object MusicScrollbar: TScrollBar
    Left = 25
    Top = 42
    Width = 369
    Height = 18
    PageSize = 0
    TabOrder = 5
    OnChange = MusicScrollbarChange
  end
  object SoundScrollbar: TScrollBar
    Left = 25
    Top = 101
    Width = 369
    Height = 17
    PageSize = 0
    TabOrder = 6
    OnChange = SoundScrollbarChange
  end
  object FullScreenCheckBox: TCheckBox
    Left = 258
    Top = 142
    Width = 102
    Height = 18
    Caption = #26159#21542#20840#23631
    TabOrder = 7
  end
  object AlphaCheckBox: TCheckBox
    Left = 258
    Top = 188
    Width = 136
    Height = 17
    Caption = #20027#35282#36974#25377#37096#20998#21322#36879#26126
    TabOrder = 8
  end
  object StartBtn: TButton
    Left = 25
    Top = 211
    Width = 118
    Height = 39
    Caption = #21551#21160#28216#25103
    TabOrder = 0
    OnClick = StartBtnClick
  end
  object SaveBtn: TButton
    Left = 150
    Top = 211
    Width = 68
    Height = 91
    Caption = #20445#23384#35774#32622
    TabOrder = 2
    OnClick = SaveBtnClick
  end
  object CmdStartBtn: TButton
    Left = 25
    Top = 263
    Width = 118
    Height = 39
    Caption = 'CMD'#27169#24335#21551#21160
    TabOrder = 1
    OnClick = CmdStartBtnClick
  end
  object EnvBtn: TButton
    Left = 309
    Top = 211
    Width = 85
    Height = 91
    Caption = #23433#35013#36816#34892#29615#22659
    TabOrder = 3
    OnClick = EnvBtnClick
  end
  object EditorBtn: TButton
    Left = 225
    Top = 211
    Width = 77
    Height = 91
    Caption = #21551#21160#32534#36753#22120
    TabOrder = 4
    OnClick = EditorBtnClick
  end
  object DisplayComboBox: TComboBox
    Left = 25
    Top = 176
    Width = 193
    Height = 21
    Margins.Left = 2
    Margins.Top = 2
    Margins.Right = 2
    Margins.Bottom = 2
    Style = csDropDownList
    ImeName = #20013#25991' ('#31616#20307') - '#25628#29399#25340#38899#36755#20837#27861
    TabOrder = 9
    OnChange = DisplayComboBoxChange
  end
  object DisplayButton: TButton
    Left = 128
    Top = 138
    Width = 90
    Height = 25
    Caption = #33258#23450#20041#20998#36776#29575
    TabOrder = 10
    OnClick = DisplayButtonClick
  end
  object DisplayModeCheckBox: TCheckBox
    Left = 258
    Top = 165
    Width = 136
    Height = 17
    Caption = #20840#23631#26102#26356#25913#20998#36776#29575
    TabOrder = 11
  end
end
