#pragma once

// Body parts: which skeleton nodes stand for a hand, a mouth, a shaft, an opening..., reading them
// every frame, and predicting the shaft for any SOSBend.
namespace Body
{
	enum Node : int
	{
		GenBase,
		Gen01,
		Gen02,
		Gen03,
		Gen04,
		Gen05,
		Gen06,
		Scrotum,
		RHand,
		RF11,
		RF21,
		RF31,
		LHand,
		LF11,
		LF21,
		LF31,
		Mouth00,  // behind the lips, ~5 units in front of the head bone
		MouthU,   // upper lip
		MouthD,   // lower lip
		MouthL,   // mouth corners
		MouthR,
		VaginaB1,
		VaginaDeep1,
		Anus00,
		AnusDeep1,
		RToe,
		RFoot,
		LToe,
		LFoot,
		LBreast,
		RBreast,
		kNodeCount
	};

	using Vec = RE::NiPoint3;

	// One frame of one actor: world transforms of the nodes it has.
	struct Pose
	{
		std::array<RE::NiTransform, kNodeCount> t{};
		std::uint64_t                            valid = 0;

		bool       Has(Node a_node) const { return (valid >> a_node) & 1; }
		const Vec& P(Node a_node) const { return t[a_node].translate; }
	};

	// Per actor, fixed while a scene is measured.
	struct ActorInfo
	{
		RE::ActorHandle                                         handle;
		RE::NiPointer<RE::NiAVObject>                           root3D;  // the 3D the nodes belong to
		std::array<RE::NiPointer<RE::NiAVObject>, kNodeCount> nodes{};
		Vec                                                     root;
		float                                                   heading = 0.0f;  // radians
		float                                                   tipLength = -1.0f;  // glans tip past the centre line's end, / GenBase scale; <0 = unknown
		// Where the genital mesh's centre line runs relative to each Gen bone, across the shaft in
		// the bend plane and sideways, / GenBase scale (0 = on the bone).
		std::array<float, 6>                                    centerNormal{};
		std::array<float, 6>                                    centerLateral{};
		bool                                                    centerKnown = false;
		bool                                                    female = false;

		bool HasShaft() const;
	};

	// Main thread. False if the actor has no 3D.
	bool Resolve(RE::Actor* a_actor, ActorInfo& a_info);
	// Main thread. True if the actor's 3D was rebuilt since Resolve (the cached nodes are then
	// detached and frozen); re-resolves it.
	bool Refresh(ActorInfo& a_info);
	// Main thread.
	void Read(const ActorInfo& a_info, Pose& a_pose);

	// Shaft shapes per SOSBend (data\SKSE\Plugins\OStimAutoAlign\tng_bend_shapes.json).
	bool LoadShapes();

	struct ShaftModel
	{
		int   table = 0;     // 0 = TNG female behavior, 1 = male
		float size = 1.0f;   // measured length / table length
		float                tipLength = 0.0f;
		float                fitError = 0.0f;
		std::array<float, 6> centerNormal{};
		std::array<float, 6> centerLateral{};
	};

	// Which table matches the measured chain at the bend that is playing, and its size.
	std::optional<ShaftModel> FitShaft(const std::vector<Pose>& a_frames, const ActorInfo& a_info, int a_currentBend);

	constexpr int kChainPoints = 7;  // Gen01..Gen06 + tip
	using Chain = std::array<Vec, kChainPoints>;

	// The shaft's visible centre line (Gen01..Gen06 moved onto the mesh's centre) + tip at a_bend,
	// placed by this frame's GenBase.
	Chain ShaftAt(const Pose& a_pose, const ShaftModel& a_model, int a_bend);
	// The shaft as it is in this frame.
	Chain ShaftNow(const Pose& a_pose, const ShaftModel& a_model);

	Vec  HandPoint(const Pose& a_pose, bool a_right);
	bool HasHand(const Pose& a_pose, bool a_right);
}
