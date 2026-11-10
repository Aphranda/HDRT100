using System.Collections.Generic;
using System.Windows;
using System.Windows.Controls;

namespace DhrT100.SequenceDebug.App;

/// <summary>The OUT1–OUT4 enable + role editor shared by the three modes.</summary>
public partial class OutAssignment : UserControl
{
    public static readonly DependencyProperty SlotsProperty = DependencyProperty.Register(
        nameof(Slots), typeof(IEnumerable<OutSlot>), typeof(OutAssignment),
        new PropertyMetadata(null));

    public OutAssignment()
    {
        InitializeComponent();
    }

    public IEnumerable<OutSlot>? Slots
    {
        get => (IEnumerable<OutSlot>?)GetValue(SlotsProperty);
        set => SetValue(SlotsProperty, value);
    }
}
