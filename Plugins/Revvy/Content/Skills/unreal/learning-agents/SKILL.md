---
name: learning-agents
display_name: Learning Agents (ML)
description: Epic's official machine-learning plugin for training NPCs via reinforcement
  and imitation learning. Knowledge pack — use when the user asks about training AI
  characters, RL in Unreal, imitation learning, or the Learning Agents plugin.
unreal_classes:
  - ULearningAgentsManager
  - ULearningAgentsInteractor
  - ULearningAgentsTrainer
  - ULearningAgentsPolicy
keywords:
  - learning agents
  - machine learning
  - reinforcement learning
  - imitation learning
  - RL
  - train
  - NPC AI
  - neural network
  - policy
  - reward
related_skills:
  - ue-testing-debugging
---

# Learning Agents (Epic ML plugin) Skill

> Knowledge pack. Revvy has no dedicated Learning Agents service — drive setup through
> normal Blueprint/C++ editing tools. **Verify member-level API against the installed
> engine (`lookup_class`) before writing code** — this plugin's API shifted between
> 5.3 → 5.5 → 5.8 and tutorials go stale fast.

## What it is

Official plugin (Plugins → "Learning Agents", Experimental→Beta track) for training
game AI with **reinforcement learning** (reward-driven) and **imitation learning**
(mimic recorded human play). Training runs PPO-style against a Python backend the
plugin manages; inference runs in-engine via a neural-network asset. Distinct from
**ML Adapter** (external-process RPC to query game state / drive actors) and from
**NNE** (generic neural-net inference runtime).

## Core object taxonomy (stable names)

| Class | Role |
|---|---|
| `ULearningAgentsManager` | Component that owns the agent set; agents register with it |
| `ULearningAgentsInteractor` | Defines **observations** (what agents see) and **actions** (what they can do) |
| `ULearningAgentsTrainer` | Defines **rewards** and **completions** (episode end); drives training |
| `ULearningAgentsPolicy` | The trained network mapping observations → actions |
| `ULearningAgentsCritic` | Value estimator used during training |
| Recorder / ImitationTrainer classes | Capture demonstrations, then train against them |

Typical flow: subclass Interactor + Trainer (Blueprint or C++) → specify observations,
actions, rewards, completions → run training (editor ticks episodes, Python process
learns) → save the neural-network asset → ship Policy in inference mode.

## When to recommend what

- "NPC가 스스로 배우게" / emergent behavior / hard-to-script control → Learning Agents RL.
- "사람 플레이 따라하게" → imitation: record demonstrations first, expect RL fine-tuning after.
- Deterministic, debuggable game logic → **not** ML; Behavior Trees / State Trees (`ue-state-trees`).
- Model trained elsewhere (ONNX) just needs to run → NNE, not Learning Agents.

## Practical notes

- Official course + per-version tutorials live on dev.epicgames.com ("Learning Agents 5.x") — pick the one matching the installed engine.
- Training is slow and stochastic: budget many episodes, seed variation, and expect
  reward-shaping iteration; verify setups in PIE (`pie-testing`) with tiny agent counts first.
- Rewards/completions are the real design surface — most "it won't learn" reports are reward bugs, not plugin bugs.
