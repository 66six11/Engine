using System;
using System.Collections.Generic;
using System.Linq;

namespace Editor.Core.CodeFirstUI;

public sealed class GuiFrameBuilder
{
    private readonly string panelId_;
    private readonly MutableGuiNode root_;
    private readonly Stack<MutableGuiNode> nodeStack_ = new();

    public GuiFrameBuilder(string panelId)
    {
        if (string.IsNullOrWhiteSpace(panelId))
        {
            throw new ArgumentException("Panel id must not be empty.", nameof(panelId));
        }

        panelId_ = panelId;
        root_ = new MutableGuiNode(
            new GuiNodeId(panelId_, string.Empty, GuiNodeKind.Root),
            GuiNodeKind.Root,
            Label: null);
        nodeStack_.Push(root_);
    }

    public void Label(string key, string label)
    {
        AddLeaf(key, GuiNodeKind.Label, label);
    }

    public void Button(string key, string label)
    {
        AddLeaf(key, GuiNodeKind.Button, label);
    }

    public IDisposable Toolbar(string key)
    {
        return PushContainer(key, GuiNodeKind.Toolbar, label: null);
    }

    public IDisposable Vertical(string key)
    {
        return PushContainer(key, GuiNodeKind.Vertical, label: null);
    }

    public GuiTreeSnapshot Build()
    {
        return new GuiTreeSnapshot(panelId_, ToImmutable(root_));
    }

    private void AddLeaf(string key, GuiNodeKind kind, string? label)
    {
        var node = new MutableGuiNode(CreateNodeId(key, kind), kind, label);
        nodeStack_.Peek().Children.Add(node);
    }

    private IDisposable PushContainer(string key, GuiNodeKind kind, string? label)
    {
        var node = new MutableGuiNode(CreateNodeId(key, kind), kind, label);
        nodeStack_.Peek().Children.Add(node);
        nodeStack_.Push(node);
        return new GuiScope(this, node);
    }

    private GuiNodeId CreateNodeId(string key, GuiNodeKind kind)
    {
        if (string.IsNullOrWhiteSpace(key))
        {
            throw new ArgumentException("Node key must not be empty.", nameof(key));
        }

        var parentKeyPath = nodeStack_.Peek().Id.KeyPath;
        var keyPath = string.IsNullOrWhiteSpace(parentKeyPath)
            ? key
            : $"{parentKeyPath}/{key}";

        return new GuiNodeId(panelId_, keyPath, kind);
    }

    private void PopScope(MutableGuiNode expectedNode)
    {
        if (nodeStack_.Count <= 1 || !ReferenceEquals(nodeStack_.Peek(), expectedNode))
        {
            throw new InvalidOperationException("GUI scope disposal order does not match creation order.");
        }

        nodeStack_.Pop();
    }

    private static GuiNode ToImmutable(MutableGuiNode node)
    {
        return new GuiNode(
            node.Id,
            node.Kind,
            node.Label,
            node.Children.Select(ToImmutable).ToArray());
    }

    private sealed record MutableGuiNode(
        GuiNodeId Id,
        GuiNodeKind Kind,
        string? Label)
    {
        public List<MutableGuiNode> Children { get; } = [];
    }

    private sealed class GuiScope : IDisposable
    {
        private readonly GuiFrameBuilder builder_;
        private readonly MutableGuiNode node_;
        private bool isDisposed_;

        public GuiScope(GuiFrameBuilder builder, MutableGuiNode node)
        {
            builder_ = builder;
            node_ = node;
        }

        public void Dispose()
        {
            if (isDisposed_)
            {
                return;
            }

            builder_.PopScope(node_);
            isDisposed_ = true;
        }
    }
}
