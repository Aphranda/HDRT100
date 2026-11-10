using System.Windows;

namespace DhrT100.SequenceDebug.App;

public partial class BudgetWindow : Window
{
    private readonly BudgetDialogModel _model;

    public BudgetWindow(MainViewModel main)
    {
        InitializeComponent();
        _model = new BudgetDialogModel(main);
        DataContext = _model;
    }

    public void Read()
    {
        _model.Read();
    }

    public void Invalidate()
    {
        _model.Invalidate();
    }
}
