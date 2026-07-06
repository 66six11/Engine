using System;
using System.IO;
using System.Linq;
using Xunit;

namespace Editor.Tests.Architecture;

public sealed class StudioLayeringTests
{
    [Fact]
    public void Ordinary_features_do_not_depend_on_shell_services_namespace()
    {
        var root = FindRepositoryRoot();
        var featureFiles = Directory.EnumerateFiles(
            Path.Combine(root, "Features"),
            "*.cs",
            SearchOption.AllDirectories);

        var offenders = featureFiles
            .Select(path => Path.GetRelativePath(root, path))
            .Where(path => !path.StartsWith(
                Path.Combine("Features", "Workbench") + Path.DirectorySeparatorChar,
                StringComparison.Ordinal))
            .Where(path => File.ReadAllText(Path.Combine(root, path)).Contains(
                "using Editor.Shell.Services;",
                StringComparison.Ordinal))
            .Order(StringComparer.Ordinal)
            .ToArray();

        Assert.Empty(offenders);
    }

    [Fact]
    public void Editor_ui_dispatcher_contract_lives_in_core_abstractions()
    {
        var root = FindRepositoryRoot();

        Assert.True(
            File.Exists(Path.Combine(root, "Core", "Abstractions", "IEditorUiDispatcher.cs")),
            "IEditorUiDispatcher is a cross-layer contract and should live in Core/Abstractions.");
        Assert.False(
            File.Exists(Path.Combine(root, "Shell", "Services", "IEditorUiDispatcher.cs")),
            "Shell should keep AvaloniaEditorUiDispatcher as the implementation, not the dispatcher contract.");
    }

    [Fact]
    public void Core_code_first_ui_does_not_reference_avalonia_or_shell()
    {
        var root = FindRepositoryRoot();
        var files = Directory.EnumerateFiles(
            Path.Combine(root, "Core", "CodeFirstUI"),
            "*.cs",
            SearchOption.AllDirectories);

        var offenders = files
            .Where(path =>
            {
                var text = File.ReadAllText(path);
                return text.Contains("Avalonia", StringComparison.Ordinal)
                    || text.Contains("Editor.Shell", StringComparison.Ordinal);
            })
            .Select(path => Path.GetRelativePath(root, path))
            .Order(StringComparer.Ordinal)
            .ToArray();

        Assert.Empty(offenders);
    }

    [Fact]
    public void Shell_code_first_ui_does_not_reference_features_or_native_runtime()
    {
        var root = FindRepositoryRoot();
        var files = Directory.EnumerateFiles(
                Path.Combine(root, "Shell", "CodeFirstUI"),
                "*.*",
                SearchOption.AllDirectories)
            .Where(path => path.EndsWith(".cs", StringComparison.Ordinal)
                || path.EndsWith(".axaml", StringComparison.Ordinal));

        var offenders = files
            .Where(path =>
            {
                var text = File.ReadAllText(path);
                return text.Contains("Editor.Features", StringComparison.Ordinal)
                    || text.Contains("Vulkan", StringComparison.Ordinal)
                    || text.Contains("Native", StringComparison.Ordinal);
            })
            .Select(path => Path.GetRelativePath(root, path))
            .Order(StringComparer.Ordinal)
            .ToArray();

        Assert.Empty(offenders);
    }

    [Fact]
    public void Editor_icon_types_live_in_ui_icons()
    {
        var root = FindRepositoryRoot();

        Assert.True(
            File.Exists(Path.Combine(root, "UI", "Icons", "EditorIconKey.cs")),
            "EditorIconKey is shared visual vocabulary and should live in UI/Icons.");
        Assert.True(
            File.Exists(Path.Combine(root, "UI", "Icons", "EditorIconRegistry.cs")),
            "EditorIconRegistry is shared visual infrastructure and should live in UI/Icons.");
        Assert.True(
            File.Exists(Path.Combine(root, "UI", "Icons", "EditorIconView.cs")),
            "EditorIconView is a reusable Avalonia visual control and should live in UI/Icons.");
        Assert.False(
            Directory.Exists(Path.Combine(root, "Shell", "Icons")),
            "Shell should consume shared icons from UI, not own the icon system.");
    }

    [Fact]
    public void Source_files_do_not_reference_shell_icons_namespace()
    {
        var root = FindRepositoryRoot();
        var shellIconsNamespace = "Editor.Shell." + "Icons";
        var shellIconsXmlNamespace = "using:Editor.Shell." + "Icons";
        var sourceFiles = new[] { "Core", "Shell", "UI", "Features", "Tests" }
            .SelectMany(directory => Directory.EnumerateFiles(
                Path.Combine(root, directory),
                "*.*",
                SearchOption.AllDirectories))
            .Where(path => path.EndsWith(".cs", StringComparison.Ordinal)
                || path.EndsWith(".axaml", StringComparison.Ordinal));

        var offenders = sourceFiles
            .Where(path =>
            {
                var text = File.ReadAllText(path);
                return text.Contains(shellIconsNamespace, StringComparison.Ordinal)
                    || text.Contains(shellIconsXmlNamespace, StringComparison.Ordinal);
            })
            .Select(path => Path.GetRelativePath(root, path))
            .Order(StringComparer.Ordinal)
            .ToArray();

        Assert.Empty(offenders);
    }

    [Fact]
    public void Shared_view_model_base_lives_in_ui_viewmodels()
    {
        var root = FindRepositoryRoot();

        Assert.True(
            File.Exists(Path.Combine(root, "UI", "ViewModels", "ViewModelBase.cs")),
            "ViewModelBase is shared presentation infrastructure and should live in UI/ViewModels.");
        Assert.False(
            File.Exists(Path.Combine(root, "Shell", "ViewModels", "ViewModelBase.cs")),
            "Shell should consume ViewModelBase from UI, not own the shared presentation base.");
    }

    [Fact]
    public void Ordinary_features_do_not_depend_on_shell_viewmodels_namespace()
    {
        var root = FindRepositoryRoot();
        var featureFiles = Directory.EnumerateFiles(
            Path.Combine(root, "Features"),
            "*.cs",
            SearchOption.AllDirectories);

        var offenders = featureFiles
            .Where(path => File.ReadAllText(path).Contains(
                "using Editor.Shell.ViewModels;",
                StringComparison.Ordinal))
            .Select(path => Path.GetRelativePath(root, path))
            .Order(StringComparer.Ordinal)
            .ToArray();

        Assert.Empty(offenders);
    }

    [Fact]
    public void Command_palette_shell_files_live_in_command_palette_folders()
    {
        var root = FindRepositoryRoot();

        Assert.True(
            File.Exists(Path.Combine(root, "Shell", "ViewModels", "CommandPalette", "CommandPaletteViewModel.cs")),
            "CommandPaletteViewModel is a command palette surface VM and should live under Shell/ViewModels/CommandPalette.");
        Assert.True(
            File.Exists(Path.Combine(root, "Shell", "ViewModels", "CommandPalette", "CommandPaletteItemViewModel.cs")),
            "CommandPaletteItemViewModel is command palette-only row state and should live under Shell/ViewModels/CommandPalette.");
        Assert.True(
            File.Exists(Path.Combine(root, "Shell", "Views", "CommandPalette", "CommandPaletteView.axaml")),
            "CommandPaletteView is a command palette surface and should live under Shell/Views/CommandPalette.");
        Assert.True(
            File.Exists(Path.Combine(root, "Shell", "Views", "CommandPalette", "CommandPaletteView.axaml.cs")),
            "CommandPaletteView code-behind should stay next to the command palette view.");

        Assert.False(File.Exists(Path.Combine(root, "Shell", "ViewModels", "CommandPaletteViewModel.cs")));
        Assert.False(File.Exists(Path.Combine(root, "Shell", "ViewModels", "CommandPaletteItemViewModel.cs")));
        Assert.False(File.Exists(Path.Combine(root, "Shell", "Views", "CommandPaletteView.axaml")));
        Assert.False(File.Exists(Path.Combine(root, "Shell", "Views", "CommandPaletteView.axaml.cs")));
    }

    [Fact]
    public void Dialog_shell_files_live_in_dialog_folders()
    {
        var root = FindRepositoryRoot();

        Assert.True(
            File.Exists(Path.Combine(root, "Shell", "ViewModels", "Dialogs", "EditorDialogHostViewModel.cs")),
            "EditorDialogHostViewModel is dialog surface state and should live under Shell/ViewModels/Dialogs.");
        Assert.True(
            File.Exists(Path.Combine(root, "Shell", "ViewModels", "Dialogs", "EditorDialogHostDesignViewModel.cs")),
            "Dialog design-time VM should stay with the dialog host VM.");
        Assert.True(
            File.Exists(Path.Combine(root, "Shell", "ViewModels", "Dialogs", "EditorDialogButtonViewModel.cs")),
            "Dialog button VM is dialog-only row state and should live under Shell/ViewModels/Dialogs.");
        Assert.True(
            File.Exists(Path.Combine(root, "Shell", "Views", "Dialogs", "EditorDialogHostView.axaml")),
            "EditorDialogHostView is a dialog surface and should live under Shell/Views/Dialogs.");
        Assert.True(
            File.Exists(Path.Combine(root, "Shell", "Views", "Dialogs", "EditorDialogHostView.axaml.cs")),
            "Dialog host view code-behind should stay next to the dialog host view.");

        Assert.False(File.Exists(Path.Combine(root, "Shell", "ViewModels", "EditorDialogHostViewModel.cs")));
        Assert.False(File.Exists(Path.Combine(root, "Shell", "ViewModels", "EditorDialogHostDesignViewModel.cs")));
        Assert.False(File.Exists(Path.Combine(root, "Shell", "ViewModels", "EditorDialogButtonViewModel.cs")));
        Assert.False(File.Exists(Path.Combine(root, "Shell", "Views", "EditorDialogHostView.axaml")));
        Assert.False(File.Exists(Path.Combine(root, "Shell", "Views", "EditorDialogHostView.axaml.cs")));
    }

    [Fact]
    public void Menu_shell_view_models_live_in_menu_folder()
    {
        var root = FindRepositoryRoot();

        Assert.True(
            File.Exists(Path.Combine(root, "Shell", "ViewModels", "Menus", "WorkbenchMenuItemViewModel.cs")),
            "WorkbenchMenuItemViewModel is menu item presentation state and should live under Shell/ViewModels/Menus.");
        Assert.True(
            File.Exists(Path.Combine(root, "Shell", "ViewModels", "Menus", "PanelMenuItemViewModel.cs")),
            "PanelMenuItemViewModel is panel menu presentation state and should live under Shell/ViewModels/Menus.");

        Assert.False(File.Exists(Path.Combine(root, "Shell", "ViewModels", "WorkbenchMenuItemViewModel.cs")));
        Assert.False(File.Exists(Path.Combine(root, "Shell", "ViewModels", "PanelMenuItemViewModel.cs")));
    }

    [Fact]
    public void Dock_shell_presentation_files_live_in_docking_folders()
    {
        var root = FindRepositoryRoot();

        var dockViewModels = new[]
        {
            "EditorDockDragStateViewModel.cs",
            "EditorDockFloatingWindowRequest.cs",
            "EditorDockFloatingWindowViewModel.cs",
            "EditorDockNodeViewModel.cs",
            "EditorDockSplitNodeViewModel.cs",
            "EditorDockTabStripItemViewModel.cs",
            "EditorDockTabViewModel.cs",
            "EditorDockWindowNodeViewModel.cs",
            "EditorDockWindowViewModel.cs",
            "EditorDockWorkspaceKind.cs",
            "EditorDockWorkspaceViewModel.cs",
        };

        foreach (var fileName in dockViewModels)
        {
            Assert.True(
                File.Exists(Path.Combine(root, "Shell", "ViewModels", "Docking", fileName)),
                $"{fileName} is Dock presentation state and should live under Shell/ViewModels/Docking.");
            Assert.False(File.Exists(Path.Combine(root, "Shell", "ViewModels", fileName)));
        }

        var dockViews = new[]
        {
            "EditorDockDropGuideView.axaml",
            "EditorDockDropGuideView.axaml.cs",
            "EditorDockFloatingPreviewView.axaml",
            "EditorDockFloatingPreviewView.axaml.cs",
            "EditorDockFloatingWindow.axaml",
            "EditorDockFloatingWindow.axaml.cs",
            "EditorDockSplitNodeView.axaml",
            "EditorDockSplitNodeView.axaml.cs",
            "EditorDockTabItemView.axaml",
            "EditorDockTabItemView.axaml.cs",
            "EditorDockTabStripView.axaml",
            "EditorDockTabStripView.axaml.cs",
            "EditorDockWindowNodeView.axaml",
            "EditorDockWindowNodeView.axaml.cs",
            "EditorDockWindowSurfaceView.axaml",
            "EditorDockWindowSurfaceView.axaml.cs",
            "EditorDockWindowView.axaml",
            "EditorDockWindowView.axaml.cs",
            "EditorDockWorkspaceView.axaml",
            "EditorDockWorkspaceView.axaml.cs",
        };

        foreach (var fileName in dockViews)
        {
            Assert.True(
                File.Exists(Path.Combine(root, "Shell", "Views", "Docking", fileName)),
                $"{fileName} is a Dock presentation view and should live under Shell/Views/Docking.");
            Assert.False(File.Exists(Path.Combine(root, "Shell", "Views", fileName)));
        }
    }

    [Fact]
    public void Panel_placeholder_shell_files_live_in_panel_folders()
    {
        var root = FindRepositoryRoot();

        Assert.True(
            File.Exists(Path.Combine(root, "Shell", "ViewModels", "Panels", "PanelPlaceholderViewModel.cs")),
            "PanelPlaceholderViewModel is panel placeholder presentation state and should live under Shell/ViewModels/Panels.");
        Assert.True(
            File.Exists(Path.Combine(root, "Shell", "Views", "Panels", "PanelPlaceholderView.axaml")),
            "PanelPlaceholderView is a panel placeholder surface and should live under Shell/Views/Panels.");
        Assert.True(
            File.Exists(Path.Combine(root, "Shell", "Views", "Panels", "PanelPlaceholderView.axaml.cs")),
            "PanelPlaceholderView code-behind should stay next to the panel placeholder view.");

        Assert.False(File.Exists(Path.Combine(root, "Shell", "ViewModels", "PanelPlaceholderViewModel.cs")));
        Assert.False(File.Exists(Path.Combine(root, "Shell", "Views", "PanelPlaceholderView.axaml")));
        Assert.False(File.Exists(Path.Combine(root, "Shell", "Views", "PanelPlaceholderView.axaml.cs")));
    }

    [Fact]
    public void Main_window_shell_files_live_in_windowing_folders()
    {
        var root = FindRepositoryRoot();

        Assert.True(
            File.Exists(Path.Combine(root, "Shell", "ViewModels", "Windowing", "MainWindowViewModel.cs")),
            "MainWindowViewModel is the root window orchestration VM and should live under Shell/ViewModels/Windowing.");
        Assert.True(
            File.Exists(Path.Combine(root, "Shell", "Views", "Windowing", "MainWindow.axaml")),
            "MainWindow is the root Avalonia window and should live under Shell/Views/Windowing.");
        Assert.True(
            File.Exists(Path.Combine(root, "Shell", "Views", "Windowing", "MainWindow.axaml.cs")),
            "MainWindow code-behind should stay next to the root window view.");

        Assert.False(File.Exists(Path.Combine(root, "Shell", "ViewModels", "MainWindowViewModel.cs")));
        Assert.False(File.Exists(Path.Combine(root, "Shell", "Views", "MainWindow.axaml")));
        Assert.False(File.Exists(Path.Combine(root, "Shell", "Views", "MainWindow.axaml.cs")));
    }

    [Fact]
    public void Panel_docking_files_live_in_docking_panel_folders()
    {
        var root = FindRepositoryRoot();
        var expectedNamespace = "namespace Editor.Shell.Docking.Panels;";
        var expectedTestNamespace = "namespace Editor.Tests.Shell.Docking.Panels;";

        var panelFiles = new[]
        {
            "PanelRegistry.cs",
            "PanelInstanceManager.cs",
        };

        foreach (var fileName in panelFiles)
        {
            var path = Path.Combine(root, "Shell", "Docking", "Panels", fileName);
            Assert.True(
                File.Exists(path),
                $"{fileName} owns panel registration or panel instance lifecycle and should live under Shell/Docking/Panels.");
            Assert.Contains(expectedNamespace, File.ReadAllText(path), StringComparison.Ordinal);
            Assert.False(File.Exists(Path.Combine(root, "Shell", "Docking", fileName)));
        }

        var panelTestFiles = new[]
        {
            "PanelRegistryTests.cs",
            "PanelInstanceManagerTests.cs",
        };

        foreach (var fileName in panelTestFiles)
        {
            var path = Path.Combine(root, "Tests", "Editor.Tests", "Shell", "Docking", "Panels", fileName);
            Assert.True(
                File.Exists(path),
                $"{fileName} should mirror Shell/Docking/Panels in the test tree.");
            Assert.Contains(expectedTestNamespace, File.ReadAllText(path), StringComparison.Ordinal);
            Assert.False(File.Exists(Path.Combine(root, "Tests", "Editor.Tests", "Shell", "Docking", fileName)));
        }
    }

    [Fact]
    public void Tab_strip_docking_files_live_in_docking_tab_strip_folders()
    {
        var root = FindRepositoryRoot();
        var expectedNamespace = "namespace Editor.Shell.Docking.TabStrips;";
        var expectedTestNamespace = "namespace Editor.Tests.Shell.Docking.TabStrips;";

        var tabStripFiles = new[]
        {
            "EditorDockTabBounds.cs",
            "EditorDockTabReorderResolver.cs",
            "EditorDockTabStripScrollController.cs",
        };

        foreach (var fileName in tabStripFiles)
        {
            var path = Path.Combine(root, "Shell", "Docking", "TabStrips", fileName);
            Assert.True(
                File.Exists(path),
                $"{fileName} owns tab strip geometry or interaction behavior and should live under Shell/Docking/TabStrips.");
            Assert.Contains(expectedNamespace, File.ReadAllText(path), StringComparison.Ordinal);
            Assert.False(File.Exists(Path.Combine(root, "Shell", "Docking", fileName)));
        }

        var tabStripTestFiles = new[]
        {
            "EditorDockTabReorderResolverTests.cs",
            "EditorDockTabStripScrollControllerTests.cs",
        };

        foreach (var fileName in tabStripTestFiles)
        {
            var path = Path.Combine(root, "Tests", "Editor.Tests", "Shell", "Docking", "TabStrips", fileName);
            Assert.True(
                File.Exists(path),
                $"{fileName} should mirror Shell/Docking/TabStrips in the test tree.");
            Assert.Contains(expectedTestNamespace, File.ReadAllText(path), StringComparison.Ordinal);
            Assert.False(File.Exists(Path.Combine(root, "Tests", "Editor.Tests", "Shell", "Docking", fileName)));
        }
    }

    [Fact]
    public void Drop_target_docking_files_live_in_docking_drop_target_folders()
    {
        var root = FindRepositoryRoot();
        var expectedNamespace = "namespace Editor.Shell.Docking.DropTargets;";
        var expectedTestNamespace = "namespace Editor.Tests.Shell.Docking.DropTargets;";

        var dropTargetFiles = new[]
        {
            "EditorDockDropGuideKind.cs",
            "EditorDockDropOperation.cs",
            "EditorDockDropTarget.cs",
            "EditorDockHitTestService.cs",
            "EditorDockSplitterBounds.cs",
            "EditorDockWindowBounds.cs",
        };

        foreach (var fileName in dropTargetFiles)
        {
            var path = Path.Combine(root, "Shell", "Docking", "DropTargets", fileName);
            Assert.True(
                File.Exists(path),
                $"{fileName} owns dock drop target vocabulary or hit testing and should live under Shell/Docking/DropTargets.");
            Assert.Contains(expectedNamespace, File.ReadAllText(path), StringComparison.Ordinal);
            Assert.False(File.Exists(Path.Combine(root, "Shell", "Docking", fileName)));
        }

        var dropTargetTestFiles = new[]
        {
            "EditorDockHitTestServiceTests.cs",
        };

        foreach (var fileName in dropTargetTestFiles)
        {
            var path = Path.Combine(root, "Tests", "Editor.Tests", "Shell", "Docking", "DropTargets", fileName);
            Assert.True(
                File.Exists(path),
                $"{fileName} should mirror Shell/Docking/DropTargets in the test tree.");
            Assert.Contains(expectedTestNamespace, File.ReadAllText(path), StringComparison.Ordinal);
            Assert.False(File.Exists(Path.Combine(root, "Tests", "Editor.Tests", "Shell", "Docking", fileName)));
        }
    }

    [Fact]
    public void Layout_docking_files_live_in_docking_layout_folder()
    {
        var root = FindRepositoryRoot();
        var expectedNamespace = "namespace Editor.Shell.Docking.Layout;";

        var layoutFiles = new[]
        {
            "EditorDockLayoutSnapshot.cs",
            "EditorDockLayoutStore.cs",
        };

        foreach (var fileName in layoutFiles)
        {
            var path = Path.Combine(root, "Shell", "Docking", "Layout", fileName);
            Assert.True(
                File.Exists(path),
                $"{fileName} owns dock layout snapshot or persistence state and should live under Shell/Docking/Layout.");
            Assert.Contains(expectedNamespace, File.ReadAllText(path), StringComparison.Ordinal);
            Assert.False(File.Exists(Path.Combine(root, "Shell", "Docking", fileName)));
        }
    }

    [Fact]
    public void Dialog_core_models_live_in_dialog_model_folder()
    {
        var root = FindRepositoryRoot();
        var expectedNamespace = "namespace Editor.Core.Models.Dialogs;";

        var dialogFiles = new[]
        {
            "EditorDialogButtonDescriptor.cs",
            "EditorDialogButtonRole.cs",
            "EditorDialogKind.cs",
            "EditorDialogRequest.cs",
            "EditorDialogResult.cs",
            "EditorDialogResultKind.cs",
        };

        foreach (var fileName in dialogFiles)
        {
            var path = Path.Combine(root, "Core", "Models", "Dialogs", fileName);
            Assert.True(
                File.Exists(path),
                $"{fileName} is dialog request/result contract data and should live under Core/Models/Dialogs.");
            Assert.Contains(expectedNamespace, File.ReadAllText(path), StringComparison.Ordinal);
            Assert.False(File.Exists(Path.Combine(root, "Core", "Models", fileName)));
        }
    }

    [Fact]
    public void Viewport_core_models_live_in_viewport_model_folder()
    {
        var root = FindRepositoryRoot();
        var expectedNamespace = "namespace Editor.Core.Models.Viewports;";

        var viewportFiles = new[]
        {
            "ViewportClockMode.cs",
            "ViewportClockSnapshot.cs",
            "ViewportCompositionCapabilitiesSnapshot.cs",
            "ViewportCompositionStatus.cs",
            "ViewportExtent.cs",
            "ViewportId.cs",
            "ViewportKind.cs",
            "ViewportNativePresentSnapshot.cs",
            "ViewportNativePresentStatus.cs",
            "ViewportRenderReason.cs",
            "ViewportRenderRequest.cs",
            "ViewportRenderResult.cs",
            "ViewportSchedulerContext.cs",
            "ViewportSchedulerOptions.cs",
            "ViewportStateSnapshot.cs",
            "ViewportUpdatePolicy.cs",
        };

        foreach (var fileName in viewportFiles)
        {
            var path = Path.Combine(root, "Core", "Models", "Viewports", fileName);
            Assert.True(
                File.Exists(path),
                $"{fileName} is viewport contract data and should live under Core/Models/Viewports.");
            Assert.Contains(expectedNamespace, File.ReadAllText(path), StringComparison.Ordinal);
            Assert.False(File.Exists(Path.Combine(root, "Core", "Models", fileName)));
        }
    }

    [Fact]
    public void Selection_core_models_live_in_selection_model_folder()
    {
        var root = FindRepositoryRoot();
        var expectedNamespace = "namespace Editor.Core.Models.Selection;";

        var selectionFiles = new[]
        {
            "EditorSelectionChangedEventArgs.cs",
            "EditorSelectionItem.cs",
            "EditorSelectionSnapshot.cs",
        };

        foreach (var fileName in selectionFiles)
        {
            var path = Path.Combine(root, "Core", "Models", "Selection", fileName);
            Assert.True(
                File.Exists(path),
                $"{fileName} is selection contract data and should live under Core/Models/Selection.");
            Assert.Contains(expectedNamespace, File.ReadAllText(path), StringComparison.Ordinal);
            Assert.False(File.Exists(Path.Combine(root, "Core", "Models", fileName)));
        }
    }

    [Fact]
    public void Diagnostic_core_models_live_in_diagnostic_model_folder()
    {
        var root = FindRepositoryRoot();
        var expectedNamespace = "namespace Editor.Core.Models.Diagnostics;";

        var diagnosticFiles = new[]
        {
            "EditorDiagnosticChannel.cs",
            "EditorDiagnosticRecord.cs",
            "EditorDiagnosticSeverity.cs",
            "EditorDiagnosticSourceDescriptor.cs",
        };

        foreach (var fileName in diagnosticFiles)
        {
            var path = Path.Combine(root, "Core", "Models", "Diagnostics", fileName);
            Assert.True(
                File.Exists(path),
                $"{fileName} is diagnostics contract data and should live under Core/Models/Diagnostics.");
            Assert.Contains(expectedNamespace, File.ReadAllText(path), StringComparison.Ordinal);
            Assert.False(File.Exists(Path.Combine(root, "Core", "Models", fileName)));
        }
    }

    [Fact]
    public void Scene_core_models_live_in_scene_model_folder()
    {
        var root = FindRepositoryRoot();
        var expectedNamespace = "namespace Editor.Core.Models.Scene;";

        var sceneFiles = new[]
        {
            "EditorProviderRoles.cs",
            "EditorProviderState.cs",
            "EditorProviderStatusSnapshot.cs",
            "SceneObjectPropertySnapshot.cs",
            "SceneObjectPropertyValueKind.cs",
            "SceneObjectSnapshot.cs",
            "SceneProviderDescriptor.cs",
            "SceneSnapshot.cs",
        };

        foreach (var fileName in sceneFiles)
        {
            var path = Path.Combine(root, "Core", "Models", "Scene", fileName);
            Assert.True(
                File.Exists(path),
                $"{fileName} is scene snapshot/provider contract data and should live under Core/Models/Scene.");
            Assert.Contains(expectedNamespace, File.ReadAllText(path), StringComparison.Ordinal);
            Assert.False(File.Exists(Path.Combine(root, "Core", "Models", fileName)));
        }
    }

    [Fact]
    public void Frame_debug_core_models_live_in_frame_debug_model_folder()
    {
        var root = FindRepositoryRoot();
        var expectedNamespace = "namespace Editor.Core.Models.FrameDebug;";

        var frameDebugFiles = new[]
        {
            "FrameDebugAccessEdgeSnapshot.cs",
            "FrameDebugCaptureSnapshot.cs",
            "FrameDebugCommandSnapshot.cs",
            "FrameDebugDependencyEdgeSnapshot.cs",
            "FrameDebugExecutionEventSnapshot.cs",
            "FrameDebuggerSnapshot.cs",
            "FrameDebuggerState.cs",
            "FrameDebugModelGuard.cs",
            "FrameDebugPassSnapshot.cs",
            "FrameDebugPreviewSnapshot.cs",
            "FrameDebugResourceSnapshot.cs",
            "FrameDebugTransitionSnapshot.cs",
        };

        foreach (var fileName in frameDebugFiles)
        {
            var path = Path.Combine(root, "Core", "Models", "FrameDebug", fileName);
            Assert.True(
                File.Exists(path),
                $"{fileName} is frame debugger snapshot contract data and should live under Core/Models/FrameDebug.");
            Assert.Contains(expectedNamespace, File.ReadAllText(path), StringComparison.Ordinal);
            Assert.False(File.Exists(Path.Combine(root, "Core", "Models", fileName)));
        }
    }

    [Fact]
    public void Workbench_core_models_live_in_workbench_model_folder()
    {
        var root = FindRepositoryRoot();
        var expectedNamespace = "namespace Editor.Core.Models.Workbench;";

        var workbenchFiles = new[]
        {
            "EditorCommandFeedbackSeverity.cs",
            "EditorCommandFeedbackSnapshot.cs",
            "WorkbenchActionDescriptor.cs",
            "WorkbenchActionKind.cs",
            "WorkbenchActionScope.cs",
            "WorkbenchCommandExecutionResult.cs",
            "WorkbenchCommandExecutionStatus.cs",
        };

        foreach (var fileName in workbenchFiles)
        {
            var path = Path.Combine(root, "Core", "Models", "Workbench", fileName);
            Assert.True(
                File.Exists(path),
                $"{fileName} is workbench command/action contract data and should live under Core/Models/Workbench.");
            Assert.Contains(expectedNamespace, File.ReadAllText(path), StringComparison.Ordinal);
            Assert.False(File.Exists(Path.Combine(root, "Core", "Models", fileName)));
        }
    }

    [Fact]
    public void Panel_core_models_live_in_panel_model_folder()
    {
        var root = FindRepositoryRoot();
        var expectedNamespace = "namespace Editor.Core.Models.Panels;";

        var panelFiles = new[]
        {
            "DockArea.cs",
            "DockContentCachePolicy.cs",
            "EditorPanelContentModelKind.cs",
            "EditorPanelContentModelReference.cs",
            "EditorPanelContributionDescriptor.cs",
            "EditorPanelFrameContext.cs",
            "EditorPanelFrameUpdateDescriptor.cs",
            "EditorPanelFrameUpdateMode.cs",
            "EditorPanelFrameUpdateRequest.cs",
            "EditorPanelLifecycleContext.cs",
            "EditorPanelLifecycleDescriptor.cs",
            "EditorPanelLifecycleMode.cs",
            "PanelDescriptor.cs",
            "PanelKind.cs",
        };

        foreach (var fileName in panelFiles)
        {
            var path = Path.Combine(root, "Core", "Models", "Panels", fileName);
            Assert.True(
                File.Exists(path),
                $"{fileName} is panel contract data and should live under Core/Models/Panels.");
            Assert.Contains(expectedNamespace, File.ReadAllText(path), StringComparison.Ordinal);
            Assert.False(File.Exists(Path.Combine(root, "Core", "Models", fileName)));
        }
    }

    [Fact]
    public void Contribution_core_models_live_in_contribution_model_folder()
    {
        var root = FindRepositoryRoot();
        var expectedNamespace = "namespace Editor.Core.Models.Contributions;";

        var contributionFiles = new[]
        {
            "EditorActionContributionDescriptor.cs",
            "EditorContributionDescriptorSet.cs",
            "EditorContributionSourceId.cs",
            "EditorContributionSourceKind.cs",
            "EditorContributionValidationContext.cs",
            "EditorContributionValidationError.cs",
            "EditorContributionValidationResult.cs",
        };

        foreach (var fileName in contributionFiles)
        {
            var path = Path.Combine(root, "Core", "Models", "Contributions", fileName);
            Assert.True(
                File.Exists(path),
                $"{fileName} is contribution descriptor/source/validation contract data and should live under Core/Models/Contributions.");
            Assert.Contains(expectedNamespace, File.ReadAllText(path), StringComparison.Ordinal);
            Assert.False(File.Exists(Path.Combine(root, "Core", "Models", fileName)));
        }
    }

    [Fact]
    public void Background_task_core_models_live_in_background_task_model_folder()
    {
        var root = FindRepositoryRoot();
        var expectedNamespace = "namespace Editor.Core.Models.BackgroundTasks;";

        var backgroundTaskFiles = new[]
        {
            "EditorBackgroundTaskId.cs",
            "EditorBackgroundTaskSnapshot.cs",
            "EditorBackgroundTaskState.cs",
        };

        foreach (var fileName in backgroundTaskFiles)
        {
            var path = Path.Combine(root, "Core", "Models", "BackgroundTasks", fileName);
            Assert.True(
                File.Exists(path),
                $"{fileName} is background task contract data and should live under Core/Models/BackgroundTasks.");
            Assert.Contains(expectedNamespace, File.ReadAllText(path), StringComparison.Ordinal);
            Assert.False(File.Exists(Path.Combine(root, "Core", "Models", fileName)));
        }
    }

    [Fact]
    public void Lifecycle_core_models_live_in_lifecycle_model_folder()
    {
        var root = FindRepositoryRoot();
        var expectedNamespace = "namespace Editor.Core.Models.Lifecycle;";

        var lifecycleFiles = new[]
        {
            "EditorLifecycleEventKind.cs",
            "EditorLifecycleEventSnapshot.cs",
        };

        foreach (var fileName in lifecycleFiles)
        {
            var path = Path.Combine(root, "Core", "Models", "Lifecycle", fileName);
            Assert.True(
                File.Exists(path),
                $"{fileName} is lifecycle event contract data and should live under Core/Models/Lifecycle.");
            Assert.Contains(expectedNamespace, File.ReadAllText(path), StringComparison.Ordinal);
            Assert.False(File.Exists(Path.Combine(root, "Core", "Models", fileName)));
        }
    }

    [Fact]
    public void Extension_core_models_live_in_extension_model_folder()
    {
        var root = FindRepositoryRoot();
        var expectedNamespace = "namespace Editor.Core.Models.Extensions;";
        var fileName = "EditorExtensionId.cs";

        var path = Path.Combine(root, "Core", "Models", "Extensions", fileName);
        Assert.True(
            File.Exists(path),
            $"{fileName} is extension identity contract data and should live under Core/Models/Extensions.");
        Assert.Contains(expectedNamespace, File.ReadAllText(path), StringComparison.Ordinal);
        Assert.False(File.Exists(Path.Combine(root, "Core", "Models", fileName)));
    }

    [Fact]
    public void Editing_core_models_live_in_editing_model_folder()
    {
        var root = FindRepositoryRoot();
        var expectedNamespace = "namespace Editor.Core.Models.Editing;";

        var editingFiles = new[]
        {
            "EditorEditCommandDescriptor.cs",
            "EditorEditMergePolicy.cs",
            "EditorEditValidationResult.cs",
        };

        foreach (var fileName in editingFiles)
        {
            var path = Path.Combine(root, "Core", "Models", "Editing", fileName);
            Assert.True(
                File.Exists(path),
                $"{fileName} is editing command contract data and should live under Core/Models/Editing.");
            Assert.Contains(expectedNamespace, File.ReadAllText(path), StringComparison.Ordinal);
            Assert.False(File.Exists(Path.Combine(root, "Core", "Models", fileName)));
        }
    }

    [Fact]
    public void Transaction_core_models_live_in_transaction_model_folder()
    {
        var root = FindRepositoryRoot();
        var expectedNamespace = "namespace Editor.Core.Models.Transactions;";

        var transactionFiles = new[]
        {
            "EditorTransactionId.cs",
            "EditorTransactionServiceSnapshot.cs",
        };

        foreach (var fileName in transactionFiles)
        {
            var path = Path.Combine(root, "Core", "Models", "Transactions", fileName);
            Assert.True(
                File.Exists(path),
                $"{fileName} is transaction service contract data and should live under Core/Models/Transactions.");
            Assert.Contains(expectedNamespace, File.ReadAllText(path), StringComparison.Ordinal);
            Assert.False(File.Exists(Path.Combine(root, "Core", "Models", fileName)));
        }
    }

    [Fact]
    public void Frame_debugger_interop_files_separate_api_contracts_from_adapters()
    {
        var root = FindRepositoryRoot();
        var expectedApiNamespace = "namespace Editor.Core.Interop.FrameDebugger.Api;";
        var expectedAdapterNamespace = "namespace Editor.Core.Interop.FrameDebugger.Adapters;";

        var apiFiles = new[]
        {
            "FrameDebuggerNativeLibraryApi.cs",
            "FrameDebuggerNativeSnapshotBuffer.cs",
            "FrameDebuggerNativeStatus.cs",
            "IFrameDebuggerNativeApi.cs",
        };

        foreach (var fileName in apiFiles)
        {
            var path = Path.Combine(root, "Core", "Interop", "FrameDebugger", "Api", fileName);
            Assert.True(
                File.Exists(path),
                $"{fileName} is a raw native API contract and should live under Core/Interop/FrameDebugger/Api.");
            Assert.Contains(expectedApiNamespace, File.ReadAllText(path), StringComparison.Ordinal);
            Assert.False(File.Exists(Path.Combine(root, "Core", "Interop", fileName)));
            Assert.False(File.Exists(Path.Combine(root, "Core", "Interop", "FrameDebugger", "Adapters", fileName)));
        }

        var adapterPath = Path.Combine(
            root,
            "Core",
            "Interop",
            "FrameDebugger",
            "Adapters",
            "FrameDebuggerNativeBridge.cs");
        Assert.True(
            File.Exists(adapterPath),
            "FrameDebuggerNativeBridge implements the managed adapter and should live under Core/Interop/FrameDebugger/Adapters.");
        Assert.Contains(expectedAdapterNamespace, File.ReadAllText(adapterPath), StringComparison.Ordinal);
        Assert.False(File.Exists(Path.Combine(root, "Core", "Interop", "FrameDebuggerNativeBridge.cs")));

        var testPath = Path.Combine(
            root,
            "Tests",
            "Editor.Tests",
            "Core",
            "Interop",
            "FrameDebugger",
            "Adapters",
            "FrameDebuggerNativeBridgeTests.cs");
        Assert.True(
            File.Exists(testPath),
            "Frame debugger native bridge tests should mirror the adapter folder.");
        Assert.Contains(
            "namespace Editor.Tests.Core.Interop.FrameDebugger.Adapters;",
            File.ReadAllText(testPath),
            StringComparison.Ordinal);
        Assert.False(File.Exists(Path.Combine(
            root,
            "Tests",
            "Editor.Tests",
            "Core",
            "Interop",
            "FrameDebuggerNativeBridgeTests.cs")));
    }

    [Fact]
    public void Viewport_interop_files_separate_api_contracts_from_adapters()
    {
        var root = FindRepositoryRoot();
        var expectedApiNamespace = "namespace Editor.Core.Interop.Viewports.Api;";
        var expectedAdapterNamespace = "namespace Editor.Core.Interop.Viewports.Adapters;";

        var apiFiles = new[]
        {
            "IViewportNativeApi.cs",
            "ViewportNativeAbiHeader.cs",
            "ViewportNativeCompatibilityRequest.cs",
            "ViewportNativeCompatibilityResult.cs",
            "ViewportNativeHandleTypes.cs",
            "ViewportNativeImageFormat.cs",
            "ViewportNativeLibraryApi.cs",
            "ViewportNativePresentPacket.cs",
            "ViewportNativePresentRequest.cs",
            "ViewportNativeStatus.cs",
        };

        foreach (var fileName in apiFiles)
        {
            var path = Path.Combine(root, "Core", "Interop", "Viewports", "Api", fileName);
            Assert.True(
                File.Exists(path),
                $"{fileName} is a raw viewport native API contract and should live under Core/Interop/Viewports/Api.");
            Assert.Contains(expectedApiNamespace, File.ReadAllText(path), StringComparison.Ordinal);
            Assert.False(File.Exists(Path.Combine(root, "Core", "Interop", fileName)));
            Assert.False(File.Exists(Path.Combine(root, "Core", "Interop", "Viewports", "Adapters", fileName)));
        }

        var adapterPath = Path.Combine(
            root,
            "Core",
            "Interop",
            "Viewports",
            "Adapters",
            "ViewportNativeBridge.cs");
        Assert.True(
            File.Exists(adapterPath),
            "ViewportNativeBridge implements the managed adapter and should live under Core/Interop/Viewports/Adapters.");
        Assert.Contains(expectedAdapterNamespace, File.ReadAllText(adapterPath), StringComparison.Ordinal);
        Assert.False(File.Exists(Path.Combine(root, "Core", "Interop", "ViewportNativeBridge.cs")));

        var testPath = Path.Combine(
            root,
            "Tests",
            "Editor.Tests",
            "Core",
            "Interop",
            "Viewports",
            "Adapters",
            "ViewportNativeBridgeTests.cs");
        Assert.True(
            File.Exists(testPath),
            "Viewport native bridge tests should mirror the adapter folder.");
        Assert.Contains(
            "namespace Editor.Tests.Core.Interop.Viewports.Adapters;",
            File.ReadAllText(testPath),
            StringComparison.Ordinal);
        Assert.False(File.Exists(Path.Combine(
            root,
            "Tests",
            "Editor.Tests",
            "Core",
            "Interop",
            "ViewportNativeBridgeTests.cs")));
    }

    private static string FindRepositoryRoot()
    {
        var workspaceRoot = Environment.GetEnvironmentVariable("CODEX_WORKSPACE_ROOT");
        if (!string.IsNullOrWhiteSpace(workspaceRoot)
            && File.Exists(Path.Combine(workspaceRoot, "Editor.sln")))
        {
            return workspaceRoot;
        }

        var directory = new DirectoryInfo(Directory.GetCurrentDirectory());
        while (directory is not null)
        {
            if (File.Exists(Path.Combine(directory.FullName, "Editor.sln")))
            {
                return directory.FullName;
            }

            directory = directory.Parent;
        }

        directory = new DirectoryInfo(AppContext.BaseDirectory);
        while (directory is not null)
        {
            if (File.Exists(Path.Combine(directory.FullName, "Editor.sln")))
            {
                return directory.FullName;
            }

            directory = directory.Parent;
        }

        throw new DirectoryNotFoundException("Could not locate Editor.sln.");
    }
}
