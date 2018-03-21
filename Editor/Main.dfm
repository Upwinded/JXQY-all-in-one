object SHFEditorMainForm: TSHFEditorMainForm
  Left = 194
  Top = 111
  Caption = 'Editor for Sword Heros'#39' Fate '
  ClientHeight = 486
  ClientWidth = 624
  Color = clAppWorkSpace
  Font.Charset = DEFAULT_CHARSET
  Font.Color = clBlack
  Font.Height = -11
  Font.Name = 'Default'
  Font.Style = []
  FormStyle = fsMDIForm
  Menu = MainMenu1
  OldCreateOrder = False
  Position = poDefault
  WindowState = wsMaximized
  WindowMenu = Window1
  PixelsPerInch = 96
  TextHeight = 13
  object MainMenu1: TMainMenu
    Left = 16
    Top = 8
    object File1: TMenuItem
      Caption = #35774#32622
      Hint = 'File related commands'
      object N1: TMenuItem
        Caption = '-'
      end
      object ExitItem: TMenuItem
        Caption = #36864#20986
        Hint = 'Exit|Exit application'
        OnClick = ExitItemClick
      end
    end
    object Edit1: TMenuItem
      Caption = #32534#36753
      Hint = 'Edit commands'
      object N4: TMenuItem
        Caption = #35299#21253#19982#37325#26032#25171#21253
        OnClick = N4Click
      end
      object ImageEditorBtn: TMenuItem
        Caption = #22270#29255#32534#36753
        OnClick = ImageEditorBtnClick
      end
    end
    object Window1: TMenuItem
      Caption = #31383#21475
      Hint = 'Window related commands'
      object N2: TMenuItem
        Caption = '-'
      end
    end
    object Help1: TMenuItem
      Caption = #24110#21161
      Hint = 'Help topics'
      object HelpAboutItem: TMenuItem
        Caption = #20851#20110'...'
        Hint = 
          'About|Displays program information, version number, and copyrigh' +
          't'
        OnClick = HelpAbout1Execute
      end
    end
  end
  object TrayIcon1: TTrayIcon
    Animate = True
    PopupMenu = PopupMenu1
    Visible = True
    Left = 16
    Top = 56
  end
  object PopupMenu1: TPopupMenu
    Left = 16
    Top = 112
    object About1: TMenuItem
      Caption = 'About...'
      OnClick = About1Click
    end
    object N3: TMenuItem
      Caption = '-'
    end
    object Exit1: TMenuItem
      Caption = 'Exit'
      OnClick = Exit1Click
    end
  end
end
