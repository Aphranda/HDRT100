using System.Windows;

namespace DhrT100.SequenceDebug.App;

public partial class CounterHistoryWindow : Window
{
    public CounterHistoryWindow(MainViewModel viewModel)
    {
        InitializeComponent();
        DataContext = viewModel;
    }
}
