#pragma once
// Exploration: what the character can walk to while the player is in control (the use
// locations the scene has made available and the places it shows), each with its label,
// distance and direction from the camera's point of view; a target the player picks is
// followed by a beacon that leads along the walkable route from the navigation mesh, on the
// side of the next turn and rising as the target comes closer. In a scene that asks the
// player to look around, the beacon leads the camera to the glints. Notes and letters
// opened in the reading pane are read page by page. A target can also be walked to on its
// own, the character taking the same route the beacon leads along.

#include "features/Feature.hpp"

#include <string>
#include <vector>

namespace qa::features
{
    class ExplorationFeature : public Feature
    {
    public:
        const wchar_t* Name() const override
        {
            return L"Exploration";
        }
        void Install() override;
        void Describe(std::vector<std::wstring>& out) override;
        void Help(std::vector<std::wstring>& out) override;
    };

    // Picks the next or the previous thing to walk to and says it. Nothing happens outside
    // exploration.
    void NextTarget();
    void PreviousTarget();
    // Says the target again with its distance along the route and its direction.
    void WhereIsTarget();
    // Walks the character to the chosen target and stops on the next press. Nothing
    // happens outside exploration.
    void WalkToTarget();
    // True while the player is walking the character freely and no screen or mechanic of the
    // game is in the way.
    bool ExplorationActive();
    // True while a look-around is on: the view turned by the stick over a fixed spot, under
    // the game's bar, the phone camera, the binoculars or the rifle scope.
    bool LookAroundActive();
    // Switches the beacon off and on, and says which.
    void ToggleBeacon();
    // Puts the places of the scene's tarot cards into the list of things to walk to as
    // further ways on, or takes them out again, and says which. Nothing happens outside
    // exploration.
    void ToggleTarotCards();
    // Records that the game has offered its own place navigation (its prompts appeared).
    void NoteDestinationPrompt();
    // The label of the use location the character stands in, empty when none.
    std::wstring CurrentUseLocationLabel();
    // Writes everything the exploration reads (character, camera, use locations, places,
    // routes, glints) to the log.
    void DumpExploration();
}
