using System.Windows;

namespace DhrT100.SequenceDebug.App;

public partial class AngleWindow : Window
{
    public AngleWindow(MainViewModel viewModel)
    {
        InitializeComponent();
        DataContext = viewModel;
    }
}
