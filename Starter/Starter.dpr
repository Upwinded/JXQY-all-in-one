program Starter;

uses
  Forms,
  main in 'main.pas' {StarterForm};

{$R app.res}
{$R *.res}

begin
  Application.Initialize;
  Application.MainFormOnTaskbar := True;
  Application.CreateForm(TStarterForm, StarterForm);
  Application.Run;
end.
