#pragma once

#include "Body.h"
#include "Scenes.h"

// Finds the constant correction (one actor's offset and scale, the schlong bend) that brings the
// body parts each action of the scene uses together, over a sampled animation loop.
namespace Solver
{
	using Body::Vec;

	enum class Part
	{
		Shaft,
		Vagina,
		Anus,
		Hand,
		Mouth,  // between the lips (oral)
		Lips,   // upper lip (kissing)
		Feet,
		Breasts,
		Scrotum
	};

	struct Term
	{
		std::string action;
		Part        partA;  // on actor a
		Part        partB;  // on actor b
		int         a = 0;  // scene actor indices
		int         b = 0;
		float       lo = 0.0f, hi = 1.0f;  // part of the shaft that counts (0 = base, 1 = tip)
		float       gap = 0.0f;           // distance at which the parts touch
		int         priority = 0;
		float       weight = 1.0f;
	};

	// The scene's actions we know how to align, highest priority first (empty if none).
	std::vector<Term> TermsFor(const Scenes::Scene& a_scene, int a_actorCount);

	struct Input
	{
		std::vector<Term>                    terms;
		std::vector<Body::ActorInfo>*        actors = nullptr;   // per scene actor
		std::vector<std::vector<Body::Pose>> frames;             // [frame][actor]
		std::vector<int>                     bends;              // current SOSBend per actor
		std::vector<int>                     sceneBends;         // the scene's own SOSBend per actor
	};

	struct ActorMove
	{
		int   actor = -1;
		float scale = 1.0f;
		Vec   offset;  // world space
	};

	struct Plan
	{
		bool                   valid = false;
		std::vector<ActorMove> moves;          // actors that change (one or both of the main pair)
		int                    bendActor = -1;  // -1: no shaft involved
		int                    bend = 0;
		std::string            hands;           // "both", "R", "L" for hand/feet terms
		float                  cost = 0.0f;
		float                  baseline = 0.0f;   // cost of leaving the scene as it is
		float                  predicted = 0.0f;  // cost of the parts after the change, without move penalties
		std::string            summary;          // the moves, for the log
		std::string            detail;           // per term distances, for the log
		std::string            baselineDetail;
		std::string            keepBend;         // the best plan that leaves the bend as it is, for the log
	};

	Plan Solve(const Input& a_input);

	// Distances as the scene plays now (for verification), same format as Plan::detail.
	float Measure(const Input& a_input, std::string& a_detail);
}
