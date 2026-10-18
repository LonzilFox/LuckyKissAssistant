using System;
using System.Diagnostics;
using System.IO;
using System.IO.MemoryMappedFiles;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Threading;
using System.Windows.Interop;
using System.Collections.Generic;

namespace LuckyKissAssistant;

public partial class MainWindow : Window
{
    private const int Size = 1408;
    private const uint Magic = 0x35414B4C;
    private const int Version = 5;
    private readonly DispatcherTimer timer = new() { Interval = TimeSpan.FromMilliseconds(350) };
    private MemoryMappedFile? mapping;
    private MemoryMappedViewAccessor? view;
    private Process? game;
    private int selected = 6;
    private string uiStatus = "";
    private bool featureFetchRequested;
    private readonly Dictionary<int, TextBox> featureInputs = new();
    private readonly Dictionary<int, TextBlock> featureReadouts = new();
    private readonly Dictionary<int, Button> featureButtons = new();
    // A live snapshot is allowed to seed an untouched textbox only once.
    // Never repaint an editor while the user is deleting or entering a value.
    private readonly HashSet<int> hydratedFeatureInputs = new();
    private readonly HashSet<int> editedFeatureInputs = new();
    private bool hydratingInputs;
    private string uiError = "";
    private const long FeatureSeq = 904, FeatureIndex = 908, FeatureBits = 912;
    private const long FeatureApplied = 916, FeatureResult = 920, FeatureReadback = 924;
    private const long FeatureType = 928, QuietExit = 944;
    private const long SnapshotSeq = 952, SnapshotApplied = 956;
    private const long SnapshotValues = 960, SnapshotTypes = 1088;
    private const long FeatureMessage = 1216;
    private static readonly string[] FeatureNames = {
        "Lura · 好感度", "Riya · 好感度", "等级 · Level", "可分配点数 · Points", "经验 · Experience", "当前生命 · Health",
        "生命上限升级点 · hp_point", "生命恢复升级点 · hp_reg_point", "当前法力 · Mana", "法力恢复升级点 · mana_reg_point",
        "移动速度升级点 · speed_point", "防御升级点 · def_point", "匕首伤害升级点 · kin_damage_point",
        "剑伤害升级点 · sword_damage_point", "弓伤害升级点 · bow_damage_point", "弓装填升级点 · bow_speed_point",
        "弩伤害升级点 · crossBow_damage_point", "弩射速升级点 · crossBow_speed_point",
        "升级需要经验 · exp_nextLvl", "最大生命 · hp_max", "生命恢复 · hp_reg", "法力恢复 · mana_reg",
        "移动速度 · speed", "防御 · def", "匕首伤害 · kin_damage", "剑伤害 · sword_damage",
        "弓伤害 · bow_damage", "弓装填属性 · bow_speed", "弩伤害 · crossBow_damage", "弩射速 · crossBow_speed",
        "Lura 计算好感度", "Riya 计算好感度"
    };


     [DllImport("dwmapi.dll", PreserveSig = true)] private static extern int DwmSetWindowAttribute(IntPtr hwnd, int attribute, ref int value, int size);
    private const uint ProcessAccess = 0x0002 | 0x0008 | 0x0020 | 0x0010 | 0x0400;
    private const uint MemCommit = 0x1000, MemReserve = 0x2000, MemRelease = 0x8000, PageReadWrite = 0x04;
    [DllImport("kernel32.dll", SetLastError = true)] private static extern IntPtr OpenProcess(uint access, bool inherit, uint pid);
    [DllImport("kernel32.dll", SetLastError = true)] private static extern bool CloseHandle(IntPtr handle);
    [DllImport("kernel32.dll", SetLastError = true)] private static extern IntPtr VirtualAllocEx(IntPtr proc, IntPtr addr, UIntPtr size, uint type, uint protect);
    [DllImport("kernel32.dll", SetLastError = true)] private static extern bool VirtualFreeEx(IntPtr proc, IntPtr addr, UIntPtr size, uint freeType);
    [DllImport("kernel32.dll", SetLastError = true)] private static extern bool WriteProcessMemory(IntPtr proc, IntPtr addr, byte[] data, UIntPtr size, out UIntPtr written);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)] private static extern IntPtr GetModuleHandle(string name);
    [DllImport("kernel32.dll", CharSet = CharSet.Ansi, SetLastError = true)] private static extern IntPtr GetProcAddress(IntPtr module, string proc);
    [DllImport("kernel32.dll", SetLastError = true)] private static extern IntPtr CreateRemoteThread(IntPtr proc, IntPtr attr, UIntPtr stack, IntPtr routine, IntPtr argument, uint flags, out uint threadId);
    [DllImport("kernel32.dll", SetLastError = true)] private static extern uint WaitForSingleObject(IntPtr obj, uint timeout);
    [DllImport("kernel32.dll", SetLastError = true)] private static extern bool GetExitCodeThread(IntPtr thread, out uint code);

    public MainWindow()
    {
        InitializeComponent();
        SourceInitialized += (_, _) =>
        {
            try {
                int dark = 1;
                IntPtr h = new WindowInteropHelper(this).Handle;
                DwmSetWindowAttribute(h, 20, ref dark, sizeof(int));
                int round = 2;
                DwmSetWindowAttribute(h, 33, ref round, sizeof(int));
            }
            catch { /* Older Windows uses standard window chrome. */ }
        };
        timer.Tick += (_, _) => Poll();
        timer.Start();
        UpdateFaces();
        CreateFeatureEditor();
        Closing += (_, _) => Disconnect();
    }

    private static bool Inject(IntPtr handle, string dll, out string message)
    {
        byte[] bytes = Encoding.Unicode.GetBytes(dll + "\0");
        IntPtr address = VirtualAllocEx(handle, IntPtr.Zero, (UIntPtr)(uint)bytes.Length, MemCommit | MemReserve, PageReadWrite);
        if (address == IntPtr.Zero) { message = "远程内存申请失败：" + Marshal.GetLastWin32Error(); return false; }
        bool canFree = true;
        try
        {
            if (!WriteProcessMemory(handle, address, bytes, (UIntPtr)(uint)bytes.Length, out var written) || written.ToUInt64() != (ulong)bytes.Length)
            { message = "远程内存写入失败：" + Marshal.GetLastWin32Error(); return false; }
            var lib = GetModuleHandle("kernel32.dll");
            var loader = GetProcAddress(lib, "LoadLibraryW");
            if (loader == IntPtr.Zero) { message = "找不到 LoadLibraryW"; return false; }
            var thread = CreateRemoteThread(handle, IntPtr.Zero, UIntPtr.Zero, loader, address, 0, out _);
            if (thread == IntPtr.Zero) { message = "创建游戏内加载线程失败：" + Marshal.GetLastWin32Error(); return false; }
            try
            {
                if (WaitForSingleObject(thread, 8000) != 0)
                { canFree = false; message = "加载线程超时，建议重启游戏后再尝试"; return false; }
                if (!GetExitCodeThread(thread, out var code) || code == 0)
                { message = "游戏拒绝加载 DLL 或路径错误"; return false; }
                message = "DLL 加载请求成功，等待游戏内初始化";
                return true;
            }
            finally { CloseHandle(thread); }
        }
        finally { if (canFree) VirtualFreeEx(handle, address, UIntPtr.Zero, MemRelease); }
    }

    private void ConnectClick(object sender, RoutedEventArgs e)
    {
        if (IsConnected())
        {
            ShowInfo(view?.ReadInt32(QuietExit) != 0
                ? "此进程已执行不可恢复的退出准备；请彻底重启 Lucky Kiss 后重新连接。"
                : "已连接当前游戏进程，无需再次注入。 ");
            Poll(); return;
        }
        Disconnect();
        game = Process.GetProcessesByName("Lucky Kiss").FirstOrDefault();
        if (game == null) { ShowError("找不到 Lucky Kiss.exe，请先进入游戏。 "); return; }
        string dll = Path.Combine(AppContext.BaseDirectory, "LuckyKissMod.dll");
        if (!File.Exists(dll)) { ShowError("缺少 LuckyKissMod.dll，请先执行 run-dev.bat。 "); return; }
        try
        {
            mapping = MemoryMappedFile.CreateOrOpen($"Local\\LuckyKissAssistantV5_{game.Id}", Size, MemoryMappedFileAccess.ReadWrite);
            view = mapping.CreateViewAccessor(0, Size, MemoryMappedFileAccess.ReadWrite);
            if (view.ReadUInt32(0) != Magic || view.ReadUInt32(4) != Version)
            {
                byte[] bytes = new byte[Size];
                view.WriteArray(0, bytes, 0, bytes.Length);
                view.Write(0, Magic); view.Write(4, Version);
                view.Write(36, -1); view.Write(876, -1); view.Write(60, game.Id);
            }
            UpdateInputs();
            featureFetchRequested = false;
            IntPtr handle = OpenProcess(ProcessAccess, false, (uint)game.Id);
            if (handle == IntPtr.Zero) { ShowError("无法打开游戏进程，错误码 " + Marshal.GetLastWin32Error()); Disconnect(); return; }
            try
            {
                if (view.ReadInt32(52) != 1 && !Inject(handle, dll, out uiStatus))
                { ShowError(uiStatus); Disconnect(); return; }
                if (view.ReadInt32(52) == 1) uiStatus = "已经加载 DLL：无需重复注入。";
            }
            finally { CloseHandle(handle); }
            Poll();
        }
        catch (Exception ex) { ShowError("连接失败：" + ex.Message); Disconnect(); }
    }

    private bool IsConnected()
    {
        if (view == null || game == null) return false;
        try { return !game.HasExited && view.ReadUInt32(0) == Magic && view.ReadUInt32(4) == Version; }
        catch { return false; }
    }
    private void Disconnect()
    {
        // Leave the injected DLL in place. Patches cannot safely unload mid-frame.
        try { view?.Write(8, 0); } catch { }
        bool hadSession = view != null;
        view?.Dispose(); mapping?.Dispose(); game?.Dispose();
        view = null; mapping = null; game = null;
        featureFetchRequested = false;
        if (hadSession)
        {
            hydratedFeatureInputs.Clear(); editedFeatureInputs.Clear();
            hydratingInputs = true;
            try { foreach (var input in featureInputs.Values) input.Clear(); }
            finally { hydratingInputs = false; }
        }
    }
    private void ShowError(string message)
    {
        uiError = message;
        uiStatus = message;
        StatusText.Text = "操作失败：" + message;
        // A rejected input is not a lost connection. Keep the connection badge
        // driven exclusively by the runtime lifecycle state in Poll().
    }
    private void ShowInfo(string message)
    {
        uiError = "";
        uiStatus = message;
    }
    private void FeatureInputChanged(object sender, TextChangedEventArgs e)
    {
        if (!hydratingInputs && sender is TextBox tb && tb.Tag is int idx)
            editedFeatureInputs.Add(idx);
    }
    private void SetFeatureInput(TextBox tb, int index, string value)
    {
        hydratingInputs = true;
        try { tb.Text = value; hydratedFeatureInputs.Add(index); editedFeatureInputs.Remove(index); }
        finally { hydratingInputs = false; }
    }
    private void RestoreInputsClick(object sender, RoutedEventArgs e)
    {
        if (!IsConnected() || view == null || view.ReadInt32(SnapshotSeq) != view.ReadInt32(SnapshotApplied))
        { ShowError("尚无完整的游戏数值快照，请先连接并读取。 "); return; }
        int restored = 0;
        foreach (var pair in featureInputs)
        {
            int idx = pair.Key, kind = view.ReadInt32(SnapshotTypes + 4L*idx);
            if (kind is not (8 or 9 or 12)) continue;
            int bits = view.ReadInt32(SnapshotValues + 4L*idx);
            SetFeatureInput(pair.Value, idx, kind == 12
                ? BitConverter.Int32BitsToSingle(bits).ToString("0.###", System.Globalization.CultureInfo.InvariantCulture)
                : bits.ToString(System.Globalization.CultureInfo.InvariantCulture));
            restored++;
        }
        ShowInfo($"已从最近一次游戏快照回填 {restored} 项；尚未提交任何修改。 ");
    }
    private static string ReadUtf8(MemoryMappedViewAccessor v, long position, int size)
    {
        var bytes = new byte[size]; v.ReadArray(position, bytes, 0, bytes.Length);
        int end = Array.IndexOf(bytes, (byte)0); if (end < 0) end = bytes.Length;
        return Encoding.UTF8.GetString(bytes, 0, end);
    }
    private void Poll()
    {
        if (!IsConnected())
        {
            if (view != null) { Disconnect(); uiStatus = "游戏已退出，需要重新连接。"; }
            ConnectionBadge.Text = "未连接"; ConnectButton.Content = "连接游戏"; ConnectButton.IsEnabled = true;
            GoldSetButton.IsEnabled = false;
            foreach (var action in featureButtons.Values) action.IsEnabled = false;
            SafeExitButtonDice.IsEnabled = false; SafeExitButtonDiag.IsEnabled = false;
            ConnectionInfo.Text = uiStatus.Length > 0 ? uiStatus : "等待连接游戏。";
            FooterStatus.Text = ConnectionInfo.Text;
            return;
        }
        try
        {
            var v = view!;
            bool ready = v.ReadInt32(52) == 1;
            bool quiet = v.ReadInt32(QuietExit) != 0;
            if(ready && !featureFetchRequested && v.ReadInt32(936) != 0 && !quiet)
            {
                featureFetchRequested = true;
                RequestSnapshot();
            }
            int flags = v.ReadInt32(48), face = v.ReadInt32(16), req = v.ReadInt32(20), runtime = v.ReadInt32(24), saved = v.ReadInt32(28);
            int observedGold = v.ReadInt32(36);
            ConnectionBadge.Text = quiet ? "已停用 · 需重启游戏" : ready ? (flags != 0 ? "已连接" : "未挂钩") : "初始化中";
            ConnectButton.Content = quiet ? "请重启游戏" : "连接游戏";
            ConnectButton.IsEnabled = !quiet;
            GoldSetButton.IsEnabled = ready && !quiet;
            foreach (var action in featureButtons.Values) action.IsEnabled = ready && !quiet;
            SafeExitButtonDice.IsEnabled = ready && !quiet;
            SafeExitButtonDiag.IsEnabled = ready && !quiet;
            ConnectionInfo.Text = $"PID {game!.Id}  ·  ABI v{Version}  ·  {(ready ? "模块已初始化" : "等待 IL2CPP 初始化")}  ·  Hook {Convert.ToString(flags, 2).PadLeft(7, '0')}";
            DiceHook.Text = (flags & (1 | 2 | 32 | 64)) != 0 ? "结算入口已安装 · 尚待实战验证" : "结算入口未挂钩";
            int collect = v.ReadInt32(848), refresh = v.ReadInt32(852), diceSize = v.ReadInt32(856);
            int lastSource = v.ReadInt32(892), selectedEvents = v.ReadInt32(836);
            DiceDetails.Text = $"游戏选择：{(face is >= 1 and <= 6 ? face.ToString() : "尚未捕获")}  ·  来源 {lastSource}  ·  实际选择事件 {selectedEvents} 次\n" +
                $"结算 Collect {collect} / Refresh {refresh}  ·  停止 {v.ReadInt32(896)} / 完成 {v.ReadInt32(900)}  ·  检测骰子 {diceSize}  ·  尝试 {v.ReadInt32(40)}  ·  写入 {v.ReadInt32(44)}  ·  未同步 {v.ReadInt32(860)}";
            GoldHook.Text = v.ReadInt32(832) == 1 ? "游戏控制器已识别" : "游戏控制器尚未识别";
            string moneyInfo = observedGold >= 0 ? observedGold.ToString("N0") : "未读取";
            int dataStatus = v.ReadInt32(872), dataValue = v.ReadInt32(876);
            string dataResult = dataStatus switch { 2 when saved == req && req > 0 => "GameData 已读回", 2 => "已调用（未证实一致）", -1 => "调用异常", -2 => "参数类型不支持", _ => "未完成" };
            GoldDetails.Text = $"运行时字段：{moneyInfo}  ·  写入次数 {v.ReadInt32(844)}  ·  数据读回：{(dataValue < 0 ? "不可用" : dataValue.ToString("N0"))}\n" +
                $"请求 #{req}  ·  运行时 {(runtime == req && req != 0 ? "字段已读回" : "未验证")}  ·  GameData {dataResult} · 磁盘存档未验证";
            UpdateFeatureView(v);
            int workerState = v.ReadInt32(948);
            string workerText = workerState == 2 ? "工作线程已退出" : workerState == 1 ? "后台空闲" : "正在执行请求";
            FooterStatus.Text = $"{(quiet ? "已停用，重启游戏才能恢复" : "运行中")} · {workerText} · 数据源 {(v.ReadInt32(940) == 1 ? "已找到" : "待获取")}";
            string runtimeStatus = ReadUtf8(v, 64, 256);
            StatusText.Text = (uiError.Length > 0 ? "操作失败：" + uiError + "\n" : uiStatus.Length > 0 ? "操作提示：" + uiStatus + "\n" : "") +
                (runtimeStatus.Length > 0 ? "模块：" + runtimeStatus : "模块状态待获取");
            ReportText.Text = ReadUtf8(v, 320, 512) + $"\nlastError={v.ReadInt32(56)}; runtimeReady={ready}; hookError={v.ReadInt32(888)}; diceListErrors={v.ReadInt32(884)}; selectorCalls={v.ReadInt32(864)}/{v.ReadInt32(868)}; controllerObserved={v.ReadInt32(832)}; GameDataAvailable={v.ReadInt32(936)}; GameDataSeen={v.ReadInt32(940)}; workerIdle={v.ReadInt32(948)}; quietExit={quiet}; featureReq={v.ReadInt32(904)}; featureApplied={v.ReadInt32(916)}; featureStatus={v.ReadInt32(920)}; featureIndex={v.ReadInt32(908)}; featureType=0x{v.ReadInt32(928):X2}; featureReadbackBits={v.ReadInt32(924)}; featureWrites={v.ReadInt32(932)}; snapshot={v.ReadInt32(956)}/{v.ReadInt32(952)}; featureMessage={ReadUtf8(v,1216,192)}";
        }
        catch (Exception ex) { uiStatus = "读取通信数据失败：" + ex.Message; }
    }
    private void UpdateInputs()
    {
        if (view == null || view.ReadInt32(QuietExit) != 0) return;
        int mode = AutoMode.IsChecked == true ? 1 : ManualMode.IsChecked == true ? 2 : 0;
        view.Write(12, selected);
        view.Write(8, mode);
    }
    private void ModeChanged(object sender, RoutedEventArgs e) { if (!IsLoaded) return; UpdateInputs(); UpdateFaces(); }
    private void FaceClick(object sender, RoutedEventArgs e)
    {
        if (sender is Button b && int.TryParse(b.Tag?.ToString(), out int n) && n >= 1 && n <= 6)
        {
            selected = n;
            ManualMode.IsChecked = true;
            UpdateInputs(); UpdateFaces();
        }
    }
    private void UpdateFaces()
    {
        if (FaceGrid == null) return;
        foreach (var btn in FaceGrid.Children.OfType<Button>())
        {
            bool current = btn.Tag?.ToString() == selected.ToString();
            btn.Background = new SolidColorBrush((Color)ColorConverter.ConvertFromString(current ? "#5F3055" : "#2C2631"));
            btn.BorderBrush = new SolidColorBrush((Color)ColorConverter.ConvertFromString(current ? "#EF63AC" : "#3B303E"));
            btn.IsEnabled = ManualMode?.IsChecked == true;
        }
    }
    private void GoldClick(object sender, RoutedEventArgs e)
    {
        if (!IsConnected() || view?.ReadInt32(52) != 1) { ShowError("请先连接游戏并等待模块初始化。"); return; }
        if(view.ReadInt32(QuietExit)!=0){ShowError("已执行准备退出。请重新启动游戏，再连接后修改金币。 ");return;}
        if (!long.TryParse(GoldInput.Text, out long sum) || sum < 0 || sum > 1_000_000_000)
        { ShowError("金币数值必须是 0 至 1000000000 的整数。"); return; }
        try
        {
            view!.Write(32, (int)sum);
            view.Write(20, view.ReadInt32(20) + 1);
            if (RaiseWake()) ShowInfo("金币修改请求已提交；等待运行时与 GameData 分别读回。");
        }
        catch (Exception ex) { ShowError("金币请求写入通信区失败：" + ex.Message); }
        Poll();
    }
    private void CopyClick(object sender, RoutedEventArgs e)
    {
        string result = $"Lucky Kiss Assistant v0.0.4 (ABI v5)\n{ConnectionInfo.Text}\n{DiceHook.Text}\n{DiceDetails.Text}\n{GoldHook.Text}\n{GoldDetails.Text}\n{FeatureStatusText.Text}\n{FooterStatus.Text}\n{StatusText.Text}\n{ReportText.Text}";
        try { Clipboard.SetText(result); uiStatus = "诊断已复制"; } catch { uiStatus = "复制失败"; }
    }

    // Event-driven IPC: no periodic IL2CPP reads from the injected worker.
    private bool RaiseWake()
    {
        if (game == null) { ShowError("尚未连接游戏，无法唤醒原生模块。"); return false; }
        try
        {
            using var evt = EventWaitHandle.OpenExisting($"Local\\LuckyKissWakeV5_{game.Id}");
            if (evt.Set()) return true;
            ShowError("原生事件信号发送失败，请复制诊断并重新连接游戏。");
        }
        catch (WaitHandleCannotBeOpenedException)
        { ShowError("原生事件尚未就绪或工作线程已停止，请完全重启游戏再连接。"); }
        catch (Exception ex) { ShowError("通信唤醒失败：" + ex.Message); }
        return false;
    }
    private static string NumericLabel(int bits, int kind)
    {
        return kind switch {
            8 or 9 => bits.ToString("N0"),
            12 => BitConverter.Int32BitsToSingle(bits).ToString("0.###"),
            _ => "当前版本不支持读取"
        };
    }
    private void CreateFeatureEditor()
    {
        for(int i=0; i<32; ++i)
        {
            if(i<18)
            {
                var row = new Grid { Margin = new Thickness(0,0,0,10) };
                row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(260) });
                row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1,GridUnitType.Star) });
                row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(96) });
                var title = new TextBlock { Text = FeatureNames[i], FontSize = 12, VerticalAlignment = VerticalAlignment.Center,
                    Foreground = (Brush)FindResource("MainText") };
                Grid.SetColumn(title,0);row.Children.Add(title);
                var input = new TextBox { Height=44, FontSize=14, Margin=new Thickness(7,0,8,0), VerticalContentAlignment=VerticalAlignment.Center };
                input.Tag=i;
                input.TextChanged += FeatureInputChanged;
                input.ToolTip = i<2 ? "请输入目标好感度（0 到 100000000）" : "填写存档原始字段值；计算属性见下方只读面板";
                featureInputs[i]=input;Grid.SetColumn(input,1);row.Children.Add(input);
                var apply=new Button { Content="写入", Tag=i, Height=44, Style=(Style)FindResource("AccentButton") };
                apply.Click+=ApplyFeatureClick;featureButtons[i]=apply;Grid.SetColumn(apply,2);row.Children.Add(apply);
                var area=i<2?RelationsList:StatsList;
                area.Children.Add(row);
                var read=new TextBlock { Text="原始字段：未读取", Foreground=(Brush)FindResource("SubText"), FontSize=11, Margin=new Thickness(0,-6,0,14) };
                featureReadouts[i]=read;area.Children.Add(read);
            }
            else if(i<30)
            {
                var row=new Grid { Margin=new Thickness(0,0,0,12) };
                row.ColumnDefinitions.Add(new ColumnDefinition{Width=new GridLength(230)});
                row.ColumnDefinitions.Add(new ColumnDefinition{Width=new GridLength(120)});
                if(i!=18)
                {
                    row.ColumnDefinitions.Add(new ColumnDefinition{Width=new GridLength(1,GridUnitType.Star)});
                    row.ColumnDefinitions.Add(new ColumnDefinition{Width=new GridLength(94)});
                }
                var title=new TextBlock {Text=FeatureNames[i],VerticalAlignment=VerticalAlignment.Center,
                    Foreground=(Brush)FindResource("MainText"),FontSize=12};
                Grid.SetColumn(title,0);row.Children.Add(title);
                var val=new TextBlock {Text="未读取",Foreground=(Brush)FindResource("PrimaryLight"),FontSize=12,
                    VerticalAlignment=VerticalAlignment.Center};
                Grid.SetColumn(val,1);row.Children.Add(val);
                featureReadouts[i]=val;
                if(i!=18)
                {
                    var input=new TextBox { Height=44,FontSize=13,Margin=new Thickness(6,0,8,0),
                        VerticalContentAlignment=VerticalAlignment.Center,Tag=i,
                        ToolTip="最终属性目标值。程序将搜索对应升级点，游戏 Getter 验证不成功则自动回退。"};
                    featureInputs[i]=input;
                    input.TextChanged += FeatureInputChanged;
                    Grid.SetColumn(input,2);row.Children.Add(input);
                    var apply=new Button {Content="设目标",Tag=i,Height=44,Style=(Style)FindResource("AccentButton")};
                    apply.Click+=ApplyFeatureClick;featureButtons[i]=apply;Grid.SetColumn(apply,3);row.Children.Add(apply);
                }
                CalculatedList.Children.Add(row);
            }
        }
    }
    private void UpdateFeatureView(MemoryMappedViewAccessor v)
    {
        int req=v.ReadInt32((long)FeatureSeq),applied=v.ReadInt32((long)FeatureApplied);
        int status=v.ReadInt32((long)FeatureResult);
        int snapReq=v.ReadInt32((long)SnapshotSeq),snapApplied=v.ReadInt32((long)SnapshotApplied);
        var msg=ReadUtf8(v,FeatureMessage,192);
        string description=status switch { 1=>"GameData 字段已读回", -1=>"字段不存在或类型不支持", -2=>"输入值/类型不支持", -3=>"GameData 尚未取得", -4=>"写入后读回不符", -5=>"目标数值不可精确达到，已回退", _=>"等待操作"};
        if (v.ReadInt32(QuietExit) != 0) description = "本次游戏已停止修改，重启游戏后可继续";
        FeatureStatusText.Text=$"{description} · 请求 {req} / 完成 {applied} · {msg} · 快照 {snapApplied}/{snapReq}";
        StatsFeatureStatus.Text=FeatureStatusText.Text;
        if(snapReq==0||snapApplied!=snapReq)return;
        for(int i=0;i<32;++i)
        {
            int kind=v.ReadInt32(SnapshotTypes+4L*i);
            int bits=v.ReadInt32(SnapshotValues+4L*i);
            string label=kind==0?"当前版本未找到":"当前 " + NumericLabel(bits,kind);
            if(featureReadouts.TryGetValue(i,out var target))target.Text=i<18?"原始字段："+label:label;
            if (kind != 0 && featureInputs.TryGetValue(i,out var input) &&
                !hydratedFeatureInputs.Contains(i) && !editedFeatureInputs.Contains(i) &&
                !input.IsKeyboardFocusWithin && string.IsNullOrWhiteSpace(input.Text))
            {
                SetFeatureInput(input, i, kind == 12
                    ? BitConverter.Int32BitsToSingle(bits).ToString("0.###",System.Globalization.CultureInfo.InvariantCulture)
                    : bits.ToString(System.Globalization.CultureInfo.InvariantCulture));
            }
        }
    }
    private void RequestSnapshot()
    {
        if(!IsConnected() || view?.ReadInt32(52)!=1 || view.ReadInt32(QuietExit)!=0)return;
        try
        {
            view.Write(SnapshotSeq,view.ReadInt32(SnapshotSeq)+1);
            RaiseWake();
        }
        catch (Exception ex) { ShowError("属性读取请求失败：" + ex.Message); }
    }
    private void RefreshStatsClick(object sender,RoutedEventArgs e)=>RequestSnapshot();
    private void ApplyFeatureClick(object sender,RoutedEventArgs e)
    {
        if(!IsConnected()||view?.ReadInt32(52)!=1){ShowError("请先连接游戏并等待初始化完成。");return;}
        if(view.ReadInt32(QuietExit)!=0){ShowError("已经执行了‘准备退出’：原生工作线程已退出，当前进程不可继续写入。请完全退出并重启游戏，再重新连接。");return;}
        if(sender is not Button button || button.Tag is not int idx || !featureInputs.TryGetValue(idx,out var input))return;
        int currentType=view.ReadInt32(SnapshotTypes+4L*idx);
        int bits;
        if(currentType==12)
        {
            if(!float.TryParse(input.Text,System.Globalization.NumberStyles.Float,System.Globalization.CultureInfo.InvariantCulture,out float n)||
               !float.IsFinite(n)||n<0||n>100000000){ShowError("浮点数字必须在 0–100000000 之间。");return;}
            bits=BitConverter.SingleToInt32Bits(n);
        }
        else
        {
            if(!int.TryParse(input.Text,System.Globalization.NumberStyles.Integer,System.Globalization.CultureInfo.InvariantCulture,out bits)||bits<0||bits>100000000)
            {ShowError("请输入 0–100000000 的非负整数；浮点字段需先读取当前属性。");return;}
        }
        try
        {
            view.Write(FeatureIndex,idx);
            view.Write(FeatureBits,bits);
            view.Write(FeatureSeq,view.ReadInt32(FeatureSeq)+1);
            int req = view.ReadInt32(FeatureSeq);
            if (RaiseWake()) ShowInfo($"已发送 {FeatureNames[idx]}（请求 #{req}）；正在等待 GameData 读回。");
        }
        catch (Exception ex) { ShowError("属性写入请求异常：" + ex.Message); }
        Poll();
    }
    private bool ConfirmStop()
    {
        var dlg = new Window {
            Owner = this, Title = "退出准备确认", Width = 445, Height = 236,
            WindowStartupLocation = WindowStartupLocation.CenterOwner,
            WindowStyle = WindowStyle.None, ResizeMode = ResizeMode.NoResize,
            ShowInTaskbar = false, Background = (Brush)FindResource("Surface")
        };
        var root = new Border {
            BorderBrush = (Brush)FindResource("BorderColor"), BorderThickness = new Thickness(1),
            CornerRadius = new CornerRadius(17), Padding = new Thickness(24),
            Background = (Brush)FindResource("Surface")
        };
        var stack = new StackPanel(); root.Child = stack;
        stack.Children.Add(new TextBlock {Text="确认停止所有修改？",FontSize=18,FontWeight=FontWeights.SemiBold});
        stack.Children.Add(new TextBlock {
            Text="这会结束原生工作线程，当前游戏进程将无法再次写入。只有彻底退出并重新启动游戏才能恢复。\n如果只是临时关闭骰子功能，请选择骰子页面的‘关闭’。",
            Foreground=(Brush)FindResource("SubText"),FontSize=12,Margin=new Thickness(0,13,0,22),TextWrapping=TextWrapping.Wrap
        });
        var actions = new StackPanel {Orientation=Orientation.Horizontal,HorizontalAlignment=HorizontalAlignment.Right};
        var cancel = new Button {Content="取消", Width=105, Height=39, Margin=new Thickness(0,0,10,0)};
        cancel.Click += (_, _) => {dlg.DialogResult=false;dlg.Close();};
        var confirm = new Button {Content="确认停止", Width=117, Height=39,Style=(Style)FindResource("AccentButton")};
        confirm.Click += (_, _) => {dlg.DialogResult=true;dlg.Close();};
        actions.Children.Add(cancel); actions.Children.Add(confirm); stack.Children.Add(actions);
        dlg.Content = root;
        return dlg.ShowDialog() == true;
    }
    private void SafeExitClick(object sender,RoutedEventArgs e)
    {
        if(!IsConnected()||view==null || view.ReadInt32(QuietExit)!=0)return;
        if(!ConfirmStop())return;
        try
        {
            view.Write(QuietExit,1);
            view.Write(8,0);
            if (RaiseWake()) ShowInfo("已结束修改会话。要恢复写入必须重启游戏，不能仅按‘连接游戏’。原生 Hook 不会被强行卸载。");
        }
        catch (Exception ex) { ShowError("无法可靠提交准备退出请求：" + ex.Message); }
        Poll();
    }
    private void NavClick(object sender,RoutedEventArgs e)
    {
        if(sender is not Button b)return;
        string page=b.Tag?.ToString()??"dice";
        DicePage.Visibility=page=="dice"?Visibility.Visible:Visibility.Collapsed;
        GoldPage.Visibility=page=="gold"?Visibility.Visible:Visibility.Collapsed;
        RelationsPage.Visibility=page=="relations"?Visibility.Visible:Visibility.Collapsed;
        StatsPage.Visibility=page=="stats"?Visibility.Visible:Visibility.Collapsed;
        DiagnosticPage.Visibility=page=="diag"?Visibility.Visible:Visibility.Collapsed;
        foreach(var nav in new [] {NavDice,NavGold,NavRelations,NavStats,NavDiagnostic})
            nav.Style=(Style)FindResource(ReferenceEquals(nav,b)?"NavSelectedButton":"NavButton");
    }
    private void TitleDrag(object sender,System.Windows.Input.MouseButtonEventArgs e)
    {
        if(e.ChangedButton!=System.Windows.Input.MouseButton.Left)return;
        if(e.ClickCount==2){WindowState=WindowState==WindowState.Maximized?WindowState.Normal:WindowState.Maximized;return;}
        try { DragMove(); } catch(InvalidOperationException) { }
    }
    private void MinimizeClick(object sender,RoutedEventArgs e)=>WindowState=WindowState.Minimized;
    private void MaximizeClick(object sender,RoutedEventArgs e)=>WindowState=WindowState==WindowState.Maximized?WindowState.Normal:WindowState.Maximized;
    private void CloseClick(object sender,RoutedEventArgs e)=>Close();

}
