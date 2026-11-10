using System.Windows;

namespace DhrT100.SequenceDebug.App;

public partial class RateWindow : Window
{
    public RateWindow(MainViewModel viewModel)
    {
        InitializeComponent();
        DataContext = viewModel;
    }
}
