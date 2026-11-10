using System.Windows;

namespace DhrT100.SequenceDebug.App;

public partial class LoopbackWindow : Window
{
    private readonly LoopbackDialogModel _model;

    public LoopbackWindow(MainViewModel main, string mode)
    {
        InitializeComponent();
        _model = new LoopbackDialogModel(main, mode);
        DataContext = _model;
        Closed += (_, _) => _model.Detach();
    }
}
