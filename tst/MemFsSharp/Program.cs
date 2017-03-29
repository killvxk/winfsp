using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace MemFsSharp
{
    class Program
    {        	    
        static void Main(string[] args)
        {
            MemFs implememntation = new MemFs();
            implememntation.MountFileSysten("X:");
            Process.GetCurrentProcess().WaitForExit();

        }
    }
}
