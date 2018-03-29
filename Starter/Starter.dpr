program Starter;

uses
  Forms,
  main in 'main.pas' {StarterForm},
  sdl2 in 'Pascal-SDL-2-Headers-master\sdl2.pas';

{$R app.res}
{$R *.res}

begin
  Application.Initialize;
  Application.MainFormOnTaskbar := True;
  Application.CreateForm(TStarterForm, StarterForm);
  Application.Run;
end.
