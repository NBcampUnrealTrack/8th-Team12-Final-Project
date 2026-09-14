Option Explicit

Dim fso, shell, root, buildDir, item, script, pythonw, command
Dim candidateVersion, candidateProxy, newestVersion, newestExecutable
Set fso = CreateObject("Scripting.FileSystemObject")
Set shell = CreateObject("WScript.Shell")

Function IsAsciiInteger(value)
    Dim index, code
    If Len(value) = 0 Then
        IsAsciiInteger = False
        Exit Function
    End If
    If Len(value) > 1 And Left(value, 1) = "0" Then
        IsAsciiInteger = False
        Exit Function
    End If
    For index = 1 To Len(value)
        code = AscW(Mid(value, index, 1))
        If code < 48 Or code > 57 Then
            IsAsciiInteger = False
            Exit Function
        End If
    Next
    IsAsciiInteger = True
End Function

Function IsStableVersion(value)
    Dim parts, index
    parts = Split(value, ".")
    If UBound(parts) <> 2 Then
        IsStableVersion = False
        Exit Function
    End If
    For index = 0 To 2
        If Not IsAsciiInteger(parts(index)) Then
            IsStableVersion = False
            Exit Function
        End If
    Next
    IsStableVersion = True
End Function

Function CompareVersionPart(leftValue, rightValue)
    If Len(leftValue) > Len(rightValue) Then
        CompareVersionPart = 1
        Exit Function
    End If
    If Len(leftValue) < Len(rightValue) Then
        CompareVersionPart = -1
        Exit Function
    End If
    CompareVersionPart = StrComp(leftValue, rightValue, vbBinaryCompare)
End Function

Function IsNewerVersion(candidate, current)
    Dim candidateParts, currentParts, index, comparison
    If Not IsStableVersion(candidate) Then
        IsNewerVersion = False
        Exit Function
    End If
    If Len(current) = 0 Then
        IsNewerVersion = True
        Exit Function
    End If
    candidateParts = Split(candidate, ".")
    currentParts = Split(current, ".")
    For index = 0 To 2
        comparison = CompareVersionPart(candidateParts(index), currentParts(index))
        If comparison > 0 Then
            IsNewerVersion = True
            Exit Function
        End If
        If comparison < 0 Then
            IsNewerVersion = False
            Exit Function
        End If
    Next
    IsNewerVersion = False
End Function

root = fso.GetParentFolderName(WScript.ScriptFullName)
script = fso.BuildPath(root, "Content\Python\revvy_app.pyw")

' Select the highest valid Revvy-x.y.z.exe. PyInstaller intentionally leaves
' older release artifacts in this directory, and FileSystemObject enumeration
' order is unspecified.
buildDir = fso.BuildPath(root, "Binaries\RevvyApp")
newestVersion = ""
newestExecutable = ""
If fso.FolderExists(buildDir) Then
    For Each item In fso.GetFolder(buildDir).Files
        If Left(item.Name, 6) = "Revvy-" And LCase(fso.GetExtensionName(item.Name)) = "exe" Then
            candidateVersion = Mid(item.Name, 7, Len(item.Name) - 10)
            candidateProxy = fso.BuildPath(buildDir, "RevvyProxy-" & candidateVersion & ".exe")
            If fso.FileExists(candidateProxy) And IsNewerVersion(candidateVersion, newestVersion) Then
                newestVersion = candidateVersion
                newestExecutable = item.Path
            End If
        End If
    Next
End If
If Len(newestExecutable) > 0 Then
    On Error Resume Next
    shell.Run Chr(34) & newestExecutable & Chr(34), 0, False
    If Err.Number = 0 Then
        WScript.Quit 0
    End If
    Err.Clear
    On Error GoTo 0
End If

pythonw = shell.ExpandEnvironmentStrings("%LOCALAPPDATA%\Programs\Python\Python312\pythonw.exe")
If Not fso.FileExists(pythonw) Then
    pythonw = "pythonw.exe"
End If

command = Chr(34) & pythonw & Chr(34) & " " & Chr(34) & script & Chr(34)
On Error Resume Next
shell.Run command, 0, False
If Err.Number <> 0 Then
    MsgBox "Revvy.exe or Python with Tkinter is required.", vbCritical, "Revvy"
    WScript.Quit 1
End If
WScript.Quit 0
