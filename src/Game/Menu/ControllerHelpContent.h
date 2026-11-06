#pragma once
#include "../../Input/InputAction.h"
#include <array>
#include <string>
#include <vector>

namespace ControllerHelpContent
{
struct Line
{
    GameInput::InputAction action;
    std::string description;
    // Preview samples captured from ControllerPromptPresenter; runtime tests check all three themes.
    std::array<std::string,3> previewLabels;
};
inline const std::vector<Line>& world()
{
    static const std::vector<Line> lines=
    {
        {GameInput::InputAction::Move,"移动；轻推走、推到底跑",{"左摇杆","左摇杆","左摇杆"}},
        {GameInput::InputAction::InteractPrimary,"交谈或主互动",{"A","×","B"}},
        {GameInput::InputAction::InteractAlternate,"备用互动",{"RB","R1","R"}},
        {GameInput::InputAction::AttackPrimary,"普通攻击",{"X","□","Y"}},
        {GameInput::InputAction::CastSkill1,"武功一",{"RT","R2","ZR"}},
        {GameInput::InputAction::CastSkill2,"武功二",{"Y","△","X"}},
        {GameInput::InputAction::CastSkill3,"武功三",{"B","○","A"}},
        {GameInput::InputAction::CastSkill4,"武功四",{"RB+A","R1+×","R+B"}},
        {GameInput::InputAction::CastSkill5,"武功五",{"RB+B","R1+○","R+A"}},
        {GameInput::InputAction::UseQuickItem1,"快捷物品一",{"RB+LB","R1+L1","R+L"}},
        {GameInput::InputAction::UseQuickItem2,"快捷物品二",{"RB+X","R1+□","R+Y"}},
        {GameInput::InputAction::UseQuickItem3,"快捷物品三",{"RB+Y","R1+△","R+X"}},
        {GameInput::InputAction::Jump,"向摇杆方向跳跃",{"LB+左摇杆","L1+左摇杆","L+左摇杆"}},
        {GameInput::InputAction::CycleInteractionTarget,"切换附近目标",{"LT","L2","ZL"}},
        {GameInput::InputAction::CycleFriendlyTarget,"切换支援目标（自己或友方）",{"RB+LT","R1+L2","R+ZL"}},
        {GameInput::InputAction::ToggleMiniMap,"开关小地图",{"L3","L3","LS"}},
        {GameInput::InputAction::ToggleSit,"打坐或起身",{"R3","R3","RS"}},
        {GameInput::InputAction::OpenSystemMenu,"打开系统菜单",{"Start","Options","+"}},
    };
    return lines;
}
inline const std::vector<Line>& menu()
{
    static const std::vector<Line> lines=
    {
        {GameInput::InputAction::NavigateUp,"十字键移动焦点",{"十字键","方向键","十字键"}},
        {GameInput::InputAction::Confirm,"确认、使用、继续对话",{"A","×","B"}},
        {GameInput::InputAction::Secondary,"拿起、交换或次操作",{"X","□","Y"}},
        {GameInput::InputAction::ShowDetails,"查看详情",{"Y","△","X"}},
        {GameInput::InputAction::Cancel,"返回或取消",{"B","○","A"}},
        {GameInput::InputAction::PreviousPanel,"切换区域（LB / RB）",{"LB","L1","L"}},
        {GameInput::InputAction::PreviousPage,"翻页（LT / RT）",{"LT","L2","ZL"}},
        {GameInput::InputAction::ScrollUp,"右摇杆滚动或移动准星",{"右摇杆","右摇杆","右摇杆"}},
        {GameInput::InputAction::OpenSettings,"系统设置",{"RB+十字键上","R1+十字键上","R+十字键上"}},
        {GameInput::InputAction::OpenMemo,"记事任务",{"RB+十字键下","R1+十字键下","R+十字键下"}},
        {GameInput::InputAction::OpenEquip,"装备",{"RB+十字键左","R1+十字键左","R+十字键左"}},
        {GameInput::InputAction::OpenGoods,"包裹",{"RB+十字键右","R1+十字键右","R+十字键右"}},
    };
    return lines;
}
}
