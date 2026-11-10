using System.Globalization;
using System.Numerics;

namespace DhrT100.SequenceDebug.Core;

/// <summary>
/// Exact rational number used where Python used <c>Fraction(Decimal)</c>, so a
/// fractional angular endpoint or pulse count is rejected instead of being
/// silently rounded by binary floating point.
/// </summary>
public readonly struct Rational : IEquatable<Rational>, IComparable<Rational>
{
    public BigInteger Numerator { get; }
    public BigInteger Denominator { get; }

    public Rational(BigInteger numerator, BigInteger denominator)
    {
        if (denominator.IsZero)
        {
            throw new DivideByZeroException("denominator must not be zero");
        }
        if (denominator.Sign < 0)
        {
            numerator = -numerator;
            denominator = -denominator;
        }
        Numerator = numerator;
        Denominator = denominator;
    }

    public static Rational Zero => new(BigInteger.Zero, BigInteger.One);

    public static Rational One => new(BigInteger.One, BigInteger.One);

    public bool IsInteger => (Numerator % Denominator).IsZero;

    public int Sign => Numerator.Sign;

    public Rational Abs()
    {
        return Numerator.Sign < 0 ? new Rational(-Numerator, Denominator) : this;
    }

    public static Rational operator +(Rational left, Rational right)
    {
        return new Rational(
            left.Numerator * right.Denominator + right.Numerator * left.Denominator,
            left.Denominator * right.Denominator);
    }

    public static Rational operator -(Rational left, Rational right)
    {
        return new Rational(
            left.Numerator * right.Denominator - right.Numerator * left.Denominator,
            left.Denominator * right.Denominator);
    }

    public static Rational operator *(Rational left, Rational right)
    {
        return new Rational(left.Numerator * right.Numerator, left.Denominator * right.Denominator);
    }

    public static Rational operator /(Rational left, Rational right)
    {
        if (right.Numerator.IsZero)
        {
            throw new DivideByZeroException("division by zero");
        }
        return new Rational(left.Numerator * right.Denominator, left.Denominator * right.Numerator);
    }

    public static bool operator <(Rational left, Rational right) => left.CompareTo(right) < 0;

    public static bool operator >(Rational left, Rational right) => left.CompareTo(right) > 0;

    public static bool operator <=(Rational left, Rational right) => left.CompareTo(right) <= 0;

    public static bool operator >=(Rational left, Rational right) => left.CompareTo(right) >= 0;

    public static Rational FromDecimal(decimal value)
    {
        return Parse(value.ToString(CultureInfo.InvariantCulture));
    }

    /// <summary>Parse a decimal literal exactly, including an optional exponent.</summary>
    public static Rational Parse(string text)
    {
        var span = text.AsSpan().Trim();
        if (span.IsEmpty)
        {
            throw new FormatException("empty number");
        }

        var index = 0;
        var negative = false;
        if (span[index] == '+' || span[index] == '-')
        {
            negative = span[index] == '-';
            index++;
        }

        var digits = new System.Text.StringBuilder();
        var scale = 0;
        var anyDigit = false;
        while (index < span.Length && char.IsAsciiDigit(span[index]))
        {
            digits.Append(span[index]);
            index++;
            anyDigit = true;
        }
        if (index < span.Length && span[index] == '.')
        {
            index++;
            while (index < span.Length && char.IsAsciiDigit(span[index]))
            {
                digits.Append(span[index]);
                index++;
                scale++;
                anyDigit = true;
            }
        }
        if (!anyDigit)
        {
            throw new FormatException("invalid number");
        }

        var exponent = 0;
        if (index < span.Length && (span[index] == 'e' || span[index] == 'E'))
        {
            index++;
            var exponentSign = 1;
            if (index < span.Length && (span[index] == '+' || span[index] == '-'))
            {
                if (span[index] == '-')
                {
                    exponentSign = -1;
                }
                index++;
            }
            var start = index;
            while (index < span.Length && char.IsAsciiDigit(span[index]))
            {
                index++;
            }
            if (index == start)
            {
                throw new FormatException("invalid exponent");
            }
            exponent = exponentSign * int.Parse(span[start..index], NumberStyles.None, CultureInfo.InvariantCulture);
        }
        if (index != span.Length)
        {
            throw new FormatException("invalid number");
        }

        var numerator = BigInteger.Parse(digits.ToString(), CultureInfo.InvariantCulture);
        if (negative)
        {
            numerator = -numerator;
        }

        var power = exponent - scale;
        if (power >= 0)
        {
            return new Rational(numerator * BigInteger.Pow(10, power), BigInteger.One);
        }
        return new Rational(numerator, BigInteger.Pow(10, -power));
    }

    public int CompareTo(Rational other)
    {
        return (Numerator * other.Denominator).CompareTo(other.Numerator * Denominator);
    }

    public bool Equals(Rational other)
    {
        return Numerator == other.Numerator && Denominator == other.Denominator;
    }

    public override bool Equals(object? obj) => obj is Rational other && Equals(other);

    public override int GetHashCode() => HashCode.Combine(Numerator, Denominator);

    public static bool operator ==(Rational left, Rational right) => left.Equals(right);

    public static bool operator !=(Rational left, Rational right) => !left.Equals(right);

    public double ToDouble() => (double)Numerator / (double)Denominator;

    public override string ToString() => $"{Numerator}/{Denominator}";
}
