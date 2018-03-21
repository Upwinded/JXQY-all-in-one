object RepackForm: TRepackForm
  Left = 0
  Top = 0
  Caption = 'RePack'
  ClientHeight = 612
  ClientWidth = 784
  Color = clBtnFace
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
  PixelsPerInch = 96
  TextHeight = 13
  object Panel1: TPanel
    Left = 0
    Top = 547
    Width = 784
    Height = 65
    Align = alBottom
    TabOrder = 0
    object Button7: TButton
      Left = 40
      Top = 16
      Width = 75
      Height = 25
      Caption = #35745#31639#25991#20214'ID'
      TabOrder = 0
      OnClick = Button7Click
    end
    object Edit1: TEdit
      Left = 136
      Top = 18
      Width = 121
      Height = 21
      ImeName = #20013#25991' ('#31616#20307') - '#25628#29399#25340#38899#36755#20837#27861
      TabOrder = 1
      TextHint = #36755#20837#25991#20214#21517
    end
    object Edit2: TEdit
      Left = 296
      Top = 18
      Width = 121
      Height = 21
      ImeName = #20013#25991' ('#31616#20307') - '#25628#29399#25340#38899#36755#20837#27861
      ReadOnly = True
      TabOrder = 2
    end
  end
  object Panel3: TPanel
    Left = 0
    Top = 0
    Width = 173
    Height = 547
    Align = alLeft
    TabOrder = 1
    object Button1: TButton
      Left = 24
      Top = 17
      Width = 121
      Height = 25
      Hint = #23558'PAK'#25991#20214#35299#21253
      Caption = #35299#21253#25991#20214
      ParentShowHint = False
      ShowHint = True
      TabOrder = 0
      OnClick = Button1Click
    end
    object Button2: TButton
      Left = 24
      Top = 57
      Width = 121
      Height = 25
      Caption = #28155#21152#25171#21253#25991#20214
      TabOrder = 1
      OnClick = Button2Click
    end
    object Button3: TButton
      Left = 24
      Top = 177
      Width = 121
      Height = 25
      Hint = #23558'MPC'#12289'SHD'#12289'ASF'#25991#20214#36716#25442#20026'IMP'#25991#20214
      Caption = #21024#38500#24403#21069#34892
      ParentShowHint = False
      ShowHint = True
      TabOrder = 2
      OnClick = Button3Click
    end
    object Button4: TButton
      Left = 24
      Top = 217
      Width = 121
      Height = 25
      Caption = #28165#31354
      TabOrder = 3
      OnClick = Button4Click
    end
    object Button5: TButton
      Left = 24
      Top = 257
      Width = 121
      Height = 25
      Hint = #23558'MPC'#12289'SHD'#12289'ASF'#25991#20214#36716#25442#20026'IMP'#25991#20214
      Caption = #36716#25442#22270#29255#26684#24335
      ParentShowHint = False
      ShowHint = True
      TabOrder = 4
      OnClick = Button5Click
    end
    object Button6: TButton
      Left = 24
      Top = 495
      Width = 121
      Height = 25
      Caption = #24320#22987#37325#26032#25171#21253
      TabOrder = 5
      OnClick = Button6Click
    end
    object Button8: TButton
      Left = 24
      Top = 298
      Width = 121
      Height = 25
      Caption = #36716#25442#22270#29255'('#24102#36879#26126#24230')'
      TabOrder = 6
      OnClick = Button8Click
    end
    object Button9: TButton
      Left = 24
      Top = 98
      Width = 121
      Height = 25
      Caption = #28155#21152#30446#24405
      TabOrder = 7
      OnClick = Button9Click
    end
    object Button10: TButton
      Left = 24
      Top = 375
      Width = 121
      Height = 25
      Caption = #36716#25442#33050#26412
      TabOrder = 8
      OnClick = Button10Click
    end
    object Button11: TButton
      Left = 24
      Top = 136
      Width = 121
      Height = 25
      Caption = #28155#21152#33050#26412#30446#24405
      TabOrder = 9
      OnClick = Button11Click
    end
    object Button12: TButton
      Left = 24
      Top = 456
      Width = 121
      Height = 25
      Caption = #37325#21629#21517
      TabOrder = 10
      OnClick = Button12Click
    end
    object Button13: TButton
      Left = 24
      Top = 415
      Width = 121
      Height = 25
      Caption = #26367#25442'wav'#20026'mp3'
      TabOrder = 11
      OnClick = Button13Click
    end
    object Button14: TButton
      Left = 24
      Top = 336
      Width = 121
      Height = 25
      Caption = #21098#35009#22270#29255
      TabOrder = 12
      OnClick = Button14Click
    end
  end
  object Panel4: TPanel
    Left = 173
    Top = 0
    Width = 611
    Height = 547
    Align = alClient
    TabOrder = 2
    object ValueListEditor1: TValueListEditor
      Left = 1
      Top = 1
      Width = 609
      Height = 545
      Align = alClient
      TabOrder = 0
      TitleCaptions.Strings = (
        #25991#20214#21517#31216
        #25991#20214#25171#21253#21517#31216#25110'ID')
      ColWidths = (
        314
        289)
    end
  end
  object OpenDialog1: TOpenDialog
    Left = 464
    Top = 432
  end
  object SaveDialog1: TSaveDialog
    Left = 560
    Top = 440
  end
end
