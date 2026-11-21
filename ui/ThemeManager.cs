using System.Windows;
using System.Windows.Media;

namespace MyFileTransferAssistant;

public sealed record ThemeInfo(int Id, string Name, string LightAccent, string DarkAccent)
{
    public Brush Swatch => new SolidColorBrush((Color)ColorConverter.ConvertFromString(LightAccent));
}

/// <summary>
/// 主题只定义强调色，其余表面色由统一的浅色/深色规则生成。
/// 所有画刷都是实色，界面不使用亚克力、渐变或透明叠加。
/// </summary>
public static class ThemeManager
{
    public static readonly ThemeInfo[] Themes =
    {
        new(0,  "靛紫",   "#5C46FF", "#8B7CFF"),
        new(1,  "深绿",   "#16A34A", "#4ADE80"),
        new(2,  "樱花",   "#E11D48", "#FB7185"),
        new(3,  "海蓝",   "#2563EB", "#60A5FA"),
        new(4,  "日落橙", "#EA580C", "#FB923C"),
        new(5,  "青碧",   "#0D9488", "#2DD4BF"),
        new(6,  "玫瑰金", "#DB2777", "#F472B6"),
        new(7,  "暗夜紫", "#7E22CE", "#A78BFA"),
        new(8,  "极光",   "#0891B2", "#22D3EE"),
        new(9,  "薄荷",   "#059669", "#34D399"),
        new(10, "琥珀",   "#D97706", "#FBBF24"),
        new(11, "炭黑",   "#404046", "#A0A0AA")
    };

    private static ResourceDictionary? _current;

    public static void Apply(int themeId, bool dark)
    {
        var theme = Themes.FirstOrDefault(t => t.Id == themeId) ?? Themes[0];
        var accent = Parse(dark ? theme.DarkAccent : theme.LightAccent);
        var dict = new ResourceDictionary();

        Color sidebar, content, card, border, text, sub, muted, input, log, soft, selection, ghost, ghostHover;
        if (dark)
        {
            content = Blend(Parse("#14151F"), accent, 0.025);
            sidebar = Blend(Parse("#181926"), accent, 0.05);
            card = Blend(Parse("#202232"), accent, 0.035);
            border = Blend(Parse("#383A4E"), accent, 0.10);
            input = Blend(Parse("#1C1E2C"), accent, 0.025);
            log = Blend(Parse("#10111B"), accent, 0.025);
            text = Parse("#E8E8F0");
            sub = Parse("#A8A8BE");
            muted = Parse("#787891");
            soft = Blend(card, accent, 0.18);
            selection = Blend(card, accent, 0.28);
            ghost = Blend(card, accent, 0.08);
            ghostHover = Blend(card, accent, 0.16);
        }
        else
        {
            content = Blend(Parse("#FAFAFC"), accent, 0.012);
            sidebar = Blend(Parse("#F6F5FC"), accent, 0.025);
            card = Parse("#FFFFFF");
            border = Blend(Parse("#E4E2EE"), accent, 0.08);
            input = Parse("#FFFFFF");
            log = Blend(Parse("#F3F2FA"), accent, 0.025);
            text = Parse("#2D313E");
            sub = Parse("#484854");
            muted = Parse("#70707C");
            soft = Blend(card, accent, 0.09);
            selection = Blend(card, accent, 0.16);
            ghost = Blend(card, accent, 0.055);
            ghostHover = Blend(card, accent, 0.12);
        }

        Add(dict, "SidebarBrush", sidebar);
        Add(dict, "ContentBrush", content);
        Add(dict, "CardBrush", card);
        Add(dict, "CardBorderBrush", border);
        Add(dict, "TextBrush", text);
        Add(dict, "SubTextBrush", sub);
        Add(dict, "MutedTextBrush", muted);
        Add(dict, "AccentBrush", accent);
        Add(dict, "AccentHoverBrush", Blend(accent, Colors.White, dark ? 0.20 : 0.12));
        Add(dict, "AccentPressedBrush", Blend(accent, Colors.Black, 0.16));
        Add(dict, "AccentSoftBrush", soft);
        Add(dict, "GhostBrush", ghost);
        Add(dict, "GhostHoverBrush", ghostHover);
        Add(dict, "InputBrush", input);
        Add(dict, "InputBorderBrush", border);
        Add(dict, "ListBrush", card);
        Add(dict, "ListSelectionBrush", selection);
        Add(dict, "NavActiveBrush", selection);
        Add(dict, "NavHoverBrush", ghostHover);
        Add(dict, "DividerBrush", border);
        Add(dict, "LogBrush", log);
        Add(dict, "LogTextBrush", text);
        Add(dict, "SuccessBrush", Parse(dark ? "#34D399" : "#16A34A"));
        Add(dict, "WarningBrush", Parse("#F59E0B"));
        Add(dict, "ErrorBrush", Parse(dark ? "#F87171" : "#DC2626"));
        Add(dict, "InfoBrush", Parse("#3B82F6"));

        if (_current != null) Application.Current.Resources.MergedDictionaries.Remove(_current);
        _current = dict;
        Application.Current.Resources.MergedDictionaries.Add(dict);
    }

    private static void Add(ResourceDictionary dict, string key, Color color)
    {
        var brush = new SolidColorBrush(color);
        brush.Freeze();
        dict[key] = brush;
    }

    private static Color Parse(string hex) => (Color)ColorConverter.ConvertFromString(hex);

    private static Color Blend(Color from, Color to, double amount)
    {
        byte Mix(byte a, byte b) => (byte)Math.Round(a + (b - a) * amount);
        return Color.FromRgb(Mix(from.R, to.R), Mix(from.G, to.G), Mix(from.B, to.B));
    }
}
