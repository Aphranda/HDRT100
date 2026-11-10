using System.Collections.Generic;
using System.Windows;
using System.Windows.Controls;

namespace DhrT100.SequenceDebug.App;

/// <summary>
/// One labelled draft field. With <see cref="Options"/> set it becomes a
/// read-only combo; otherwise a text entry that writes every keystroke back to
/// the draft, exactly like a traced Tk <c>StringVar</c>.
/// </summary>
public partial class FieldBox : UserControl
{
    public static readonly DependencyProperty LabelProperty = DependencyProperty.Register(
        nameof(Label), typeof(string), typeof(FieldBox), new PropertyMetadata(string.Empty));

    public static readonly DependencyProperty ValueProperty = DependencyProperty.Register(
        nameof(Value), typeof(string), typeof(FieldBox),
        new FrameworkPropertyMetadata(string.Empty, FrameworkPropertyMetadataOptions.BindsTwoWayByDefault));

    public static readonly DependencyProperty OptionsProperty = DependencyProperty.Register(
        nameof(Options), typeof(IEnumerable<string>), typeof(FieldBox),
        new PropertyMetadata(null, OnOptionsChanged));

    public FieldBox()
    {
        InitializeComponent();
    }

    public string Label
    {
        get => (string)GetValue(LabelProperty);
        set => SetValue(LabelProperty, value);
    }

    public string Value
    {
        get => (string)GetValue(ValueProperty);
        set => SetValue(ValueProperty, value);
    }

    public IEnumerable<string>? Options
    {
        get => (IEnumerable<string>?)GetValue(OptionsProperty);
        set => SetValue(OptionsProperty, value);
    }

    private static void OnOptionsChanged(DependencyObject dependant, DependencyPropertyChangedEventArgs args)
    {
        if (dependant is FieldBox box)
        {
            box.ApplyOptions();
        }
    }

    private void ApplyOptions()
    {
        var selectable = Options is not null;
        ComboPart.Visibility = selectable ? Visibility.Visible : Visibility.Collapsed;
        TextPart.Visibility = selectable ? Visibility.Collapsed : Visibility.Visible;
    }
}
