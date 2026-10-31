#pragma once
#include "pluginTheme.h"
#include <span>
#include <type_traits>

namespace ahq::ui {
struct ParameterGroup {
    const char *title;
    int first, count;
};

template <class T> decltype(auto) groupComponent(T &item) {
    if constexpr (std::is_pointer_v<T>)
        return *item;
    else
        return (item);
}
template <class Labels, class Controls>
void layoutGroups(Labels &labels, Controls &controls, std::span<const ParameterGroup> groups,
                  juce::Rectangle<int> bounds) {
    int rows = 0;
    const auto columnsFor = [&](ParameterGroup group) {
        return bounds.getWidth() >= 300 && (group.count == 3 || group.count >= 5) ? 3 : 2;
    };
    for (auto group : groups) {
        const int columns = columnsFor(group);
        rows += (group.count + columns - 1) / columns;
    }
    const int rowHeight = (bounds.getHeight() - int(groups.size()) * 36) / juce::jmax(1, rows);
    for (auto group : groups) {
        bounds.removeFromTop(28);
        const int columns = columnsFor(group);
        const int width = bounds.getWidth() / columns;
        for (int j = 0; j < group.count; ++j) {
            const auto i = size_t(group.first + j);
            const int x = bounds.getX() + (j % columns) * width;
            const int y = bounds.getY() + (j / columns) * rowHeight;
            groupComponent(labels[i]).setBounds(x + 4, y, width - 8, 20);
            groupComponent(controls[i])
                .setBounds(x + 7, y + 22, width - 14, juce::jmax(24, rowHeight - 28));
        }
        bounds.removeFromTop(((group.count + columns - 1) / columns) * rowHeight + 8);
    }
}
template <class Labels, class Controls>
void paintGroups(juce::Graphics &g, const PluginTheme &theme, Labels &labels, Controls &controls,
                 std::span<const ParameterGroup> groups) {
    for (auto group : groups) {
        juce::Rectangle<int> box;
        for (int j = 0; j < group.count; ++j) {
            const auto i = size_t(group.first + j);
            box = box.getUnion(groupComponent(labels[i]).getBounds())
                      .getUnion(groupComponent(controls[i]).getBounds());
        }
        theme.paintGroup(g, box.expanded(5, 4).withTrimmedTop(-24).toFloat(), group.title);
    }
}
} // namespace ahq::ui
