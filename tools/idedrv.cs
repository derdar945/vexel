using System;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

public static class D {
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr FindWindowW(string c, string t);
    [DllImport("user32.dll")]
    public static extern IntPtr GetWindow(IntPtr h, uint cmd);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll", EntryPoint = "SendMessageW")]
    public static extern IntPtr SendI(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll", CharSet = CharSet.Unicode, EntryPoint = "SendMessageW")]
    public static extern IntPtr SendS(IntPtr h, uint m, IntPtr w, string l);
    [DllImport("user32.dll")]
    public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);

    public static System.Collections.Generic.List<IntPtr> Kids(IntPtr hw, string cls) {
        var r = new System.Collections.Generic.List<IntPtr>();
        IntPtr ch = GetWindow(hw, 5);
        while (ch != IntPtr.Zero) {
            var cn = new StringBuilder(64);
            GetClassNameW(ch, cn, 64);
            if (cn.ToString() == cls) r.Add(ch);
            ch = GetWindow(ch, 2);
        }
        return r;
    }

    public static string Tx(IntPtr h) {
        var t = new StringBuilder(256);
        GetWindowTextW(h, t, 256);
        return t.ToString();
    }

    public static string Gl(IntPtr h) {
        byte[] bb = new byte[8192];
        bb[0] = 200;
        bb[1] = 0;
        var g = GCHandle.Alloc(bb, GCHandleType.Pinned);
        try {
            int n = (int)SendI(h, 0x00C4, IntPtr.Zero, g.AddrOfPinnedObject());
            if (n <= 0) return "";
            return Encoding.Unicode.GetString(bb, 0, n * 2);
        } finally {
            g.Free();
        }
    }

    public static int Main(string[] args) {
        // args: title, selectFile
        string title = args.Length > 0 ? args[0] : "VexEd";
        string want = args.Length > 1 ? args[1] : "";
        IntPtr hw = IntPtr.Zero;
        for (int i = 0; i < 40 && hw == IntPtr.Zero; i++) {
            Thread.Sleep(250);
            hw = FindWindowW("VexGUIWindow", title);
        }
        if (hw == IntPtr.Zero) {
            Console.WriteLine("FAIL no window");
            return 2;
        }
        Console.WriteLine("hwnd=" + hw);
        var st0 = Kids(hw, "Static");
        foreach (var s in st0) Console.WriteLine("st=[" + Tx(s) + "]");
        var ed0 = Kids(hw, "Edit");
        Console.WriteLine("edit0=[" + Gl(ed0[0]) + "]");
        if (args.Length > 2 && args[2] == "peek") {
            PostMessageW(hw, 0x0010, IntPtr.Zero, IntPtr.Zero);
            Console.WriteLine("done");
            return 0;
        }
        var lb = Kids(hw, "ListBox")[0];
        int idx = (int)SendS(lb, 0x018F, IntPtr.Zero, want);
        Console.WriteLine("find=" + idx);
        SendI(lb, 0x0186, (IntPtr)idx, IntPtr.Zero);
        var ed = Kids(hw, "Edit");
        var btns = Kids(hw, "Button");
        IntPtr open = IntPtr.Zero, run = IntPtr.Zero;
        foreach (var b in btns) {
            string t = Tx(b);
            if (t == "Open") open = b;
            if (t == "Run") run = b;
        }
        Console.WriteLine("edits=" + ed.Count);
        SendI(open, 0x00F5, IntPtr.Zero, IntPtr.Zero);
        Thread.Sleep(1000);
        Console.WriteLine("editor=[" + Gl(ed[0]) + "]");
        SendI(run, 0x00F5, IntPtr.Zero, IntPtr.Zero);
        Thread.Sleep(4000);
        Console.WriteLine("output=[" + Gl(ed[1]) + "]");
        PostMessageW(hw, 0x0010, IntPtr.Zero, IntPtr.Zero);
        Thread.Sleep(2000);
        Console.WriteLine("done");
        return 0;
    }
}
