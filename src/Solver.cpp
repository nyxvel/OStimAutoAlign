#include "Solver.h"

#include "Settings.h"

namespace Solver
{
	namespace
	{
		using namespace Body;

		struct ActionDef
		{
			const char* name;
			Part        a;
			Part        b;
			float       lo, hi;
			float       gap;
			int         priority;
		};

		// Parts per action (OStim's action actor = a, target = b). Gaps: the shaft is ~1.7 units
		// thick, a palm ~1; lips are ~1.5 apart when they meet (upper-lip bones).
		constexpr ActionDef kDefs[] = {
			{ "vaginalsex", Part::Shaft, Part::Vagina, 0.15f, 0.85f, 0.0f, 100 },
			{ "analsex", Part::Shaft, Part::Anus, 0.15f, 0.85f, 0.0f, 99 },
			{ "deepthroating", Part::Mouth, Part::Shaft, 0.2f, 1.0f, 0.0f, 90 },
			{ "blowjob", Part::Mouth, Part::Shaft, 0.55f, 1.0f, 0.0f, 89 },
			{ "boobjob", Part::Breasts, Part::Shaft, 0.2f, 0.9f, 0.0f, 80 },
			{ "handjob", Part::Hand, Part::Shaft, 0.1f, 0.95f, 2.5f, 75 },
			{ "footjob", Part::Feet, Part::Shaft, 0.2f, 1.0f, 2.5f, 70 },
			{ "vulvaleating", Part::Mouth, Part::Vagina, 0.0f, 1.0f, 1.0f, 60 },
			{ "vulvallicking", Part::Mouth, Part::Vagina, 0.0f, 1.0f, 1.0f, 60 },
			{ "anallicking", Part::Mouth, Part::Anus, 0.0f, 1.0f, 1.0f, 59 },
			{ "rimjob", Part::Mouth, Part::Anus, 0.0f, 1.0f, 1.0f, 59 },
			{ "penilelicking", Part::Lips, Part::Shaft, 0.3f, 1.0f, 1.5f, 55 },
			{ "testicularlicking", Part::Mouth, Part::Scrotum, 0.0f, 1.0f, 2.0f, 50 },
			{ "gropingtesticles", Part::Hand, Part::Scrotum, 0.0f, 1.0f, 2.5f, 45 },
			{ "frenchkissing", Part::Lips, Part::Lips, 0.0f, 1.0f, 1.0f, 40 },
			{ "kissing", Part::Lips, Part::Lips, 0.0f, 1.0f, 1.0f, 40 },
		};

		enum class Hands
		{
			Both,
			Right,
			Left,
			Closer  // whichever is closer each frame (secondary terms)
		};

		bool Paired(Part a_part) { return a_part == Part::Hand || a_part == Part::Feet; }

		bool HasPart(const ActorInfo& a_info, Part a_part)
		{
			const auto has = [&](std::initializer_list<Node> a_nodes) {
				return std::ranges::all_of(a_nodes, [&](Node n) { return a_info.nodes[n] != nullptr; });
			};
			switch (a_part) {
			case Part::Shaft:
				return a_info.HasShaft();
			case Part::Vagina:
				return has({ VaginaB1, VaginaDeep1 });
			case Part::Anus:
				return has({ Anus00, AnusDeep1 });
			case Part::Hand:
				return has({ RHand, RF11, RF21, RF31, LHand, LF11, LF21, LF31 });
			case Part::Mouth:
				return has({ Mouth00, MouthU });
			case Part::Lips:
				return has({ Mouth00, MouthU });
			case Part::Feet:
				return has({ RToe, RFoot, LToe, LFoot });
			case Part::Breasts:
				return has({ LBreast, RBreast });
			case Part::Scrotum:
				return has({ Scrotum });
			}
			return false;
		}

		// A part in one frame: a shaft polyline or one/two points.
		struct Geo
		{
			bool                    chain = false;
			Chain                   c{};
			std::array<Vec, 2>      pts{};
			int                     count = 0;
		};

		// The middle of the mouth opening: between the four lip bones (upper, lower, corners), or
		// between the upper lip and the inside of the mouth on skeletons without them.
		Vec MouthCentre(const Pose& a_pose)
		{
			if (a_pose.Has(MouthD) && a_pose.Has(MouthL) && a_pose.Has(MouthR)) {
				return (a_pose.P(MouthU) + a_pose.P(MouthD) + a_pose.P(MouthL) + a_pose.P(MouthR)) * 0.25f;
			}
			return (a_pose.P(Mouth00) + a_pose.P(MouthU)) * 0.5f;
		}

		Geo PointsOf(const Pose& a_pose, Part a_part, Hands a_hands)
		{
			Geo g;
			const auto add = [&](const Vec& p) { g.pts[g.count++] = p; };
			switch (a_part) {
			case Part::Vagina:
				add(a_pose.P(VaginaB1));
				break;
			case Part::Anus:
				add(a_pose.P(Anus00));
				break;
			case Part::Mouth:
			case Part::Lips:
				add(MouthCentre(a_pose));
				break;
			case Part::Breasts:
				add((a_pose.P(LBreast) + a_pose.P(RBreast)) * 0.5f);
				break;
			case Part::Scrotum:
				add(a_pose.P(Scrotum));
				break;
			case Part::Hand:
				if (a_hands != Hands::Left) {
					add(HandPoint(a_pose, true));
				}
				if (a_hands != Hands::Right) {
					add(HandPoint(a_pose, false));
				}
				break;
			case Part::Feet:
				if (a_hands != Hands::Left) {
					add((a_pose.P(RToe) + a_pose.P(RFoot)) * 0.5f);
				}
				if (a_hands != Hands::Right) {
					add((a_pose.P(LToe) + a_pose.P(LFoot)) * 0.5f);
				}
				break;
			default:
				break;
			}
			return g;
		}

		// Closest point to p on the polyline, restricted to fractions lo..hi of its segments.
		Vec Closest(const Vec& a_p, const Chain& a_c, float a_lo, float a_hi)
		{
			constexpr int n = kChainPoints - 1;
			Vec           best;
			float         bestD = FLT_MAX;
			for (int i = 0; i < n; ++i) {
				const float t0 = std::max(a_lo * n - i, 0.0f);
				const float t1 = std::min(a_hi * n - i, 1.0f);
				if (t1 < 0.0f || t0 > 1.0f || t1 < t0) {
					continue;
				}
				const auto  ab = a_c[i + 1] - a_c[i];
				const float l2 = std::max(ab.Dot(ab), 1e-9f);
				const float t = std::clamp((a_p - a_c[i]).Dot(ab) / l2, t0, t1);
				const auto  q = a_c[i] + ab * t;
				const float d = (a_p - q).Length();
				if (d < bestD) {
					bestD = d;
					best = q;
				}
			}
			return best;
		}

		// One actor's change: p -> r + s (p - r) + t, scaled about its root (its feet).
		struct Xf
		{
			float s = 1.0f;
			Vec   t;
			Vec   r;

			Vec  Fwd(const Vec& p) const { return r + (p - r) * s + t; }
			Vec  Inv(const Vec& p) const { return r + (p - t - r) / s; }
			bool Moved() const { return std::abs(s - 1.0f) > 1e-4f || t.Length() > 1e-3f; }
		};

		using Moves = std::vector<Xf>;  // per scene actor

		struct Pair
		{
			int   actorP;
			Vec   p;  // untransformed point on actorP
			int   actorQ;
			Vec   q;  // untransformed point on actorQ
			float gap;
			float weight;
		};

		struct Context
		{
			const Input*                           in;
			std::vector<Term>                      terms;
			std::vector<std::optional<ShaftModel>> models;  // per actor
			int                                    bendActor = -1;
		};

		// The shaft of actor a_actor in frame a_frame at its bend.
		Chain ShaftOf(const Context& a_ctx, std::size_t a_frame, int a_actor, int a_bend)
		{
			return ShaftAt(a_ctx.in->frames[a_frame][a_actor], *a_ctx.models[a_actor], a_bend);
		}

		Geo GeoOf(const Context& a_ctx, std::size_t a_frame, int a_actor, Part a_part, Hands a_hands, const std::vector<int>& a_bends)
		{
			if (a_part == Part::Shaft) {
				Geo g;
				g.chain = true;
				g.c = ShaftOf(a_ctx, a_frame, a_actor, a_bends[a_actor]);
				return g;
			}
			return PointsOf(a_ctx.in->frames[a_frame][a_actor], a_part, a_hands);
		}

		void Transform(Geo& a_geo, const Xf& a_xf)
		{
			if (a_geo.chain) {
				for (auto& p : a_geo.c) {
					p = a_xf.Fwd(p);
				}
			} else {
				for (int i = 0; i < a_geo.count; ++i) {
					a_geo.pts[i] = a_xf.Fwd(a_geo.pts[i]);
				}
			}
		}

		// Distances (minus the contact gap) of one term in one frame with the actors moved, and the
		// matching point pairs for the least-squares step. a_hands applies to a paired part.
		void TermFrame(const Context& a_ctx, const Term& a_term, std::size_t a_frame, const Moves& a_moves, Hands a_hands,
			const std::vector<int>& a_bends, std::vector<float>* a_dists, std::vector<Pair>* a_pairs, float a_weight)
		{
			const bool closer = a_hands == Hands::Closer;
			const auto hands = closer ? Hands::Both : a_hands;
			Geo        ga = GeoOf(a_ctx, a_frame, a_term.a, a_term.partA, hands, a_bends);
			Geo        gb = GeoOf(a_ctx, a_frame, a_term.b, a_term.partB, hands, a_bends);
			Transform(ga, a_moves[a_term.a]);
			Transform(gb, a_moves[a_term.b]);
			if (ga.chain && gb.chain) {
				return;  // shaft to shaft isn't an action we align
			}
			// the point side and the other side (chain or point)
			const bool aIsPoints = !ga.chain;
			const Geo& pts = aIsPoints ? ga : gb;
			const Geo& other = aIsPoints ? gb : ga;
			const int  ptsActor = aIsPoints ? a_term.a : a_term.b;
			const int  otherActor = aIsPoints ? a_term.b : a_term.a;

			float bestD = FLT_MAX;
			Pair  bestPair{};
			for (int i = 0; i < pts.count; ++i) {
				const Vec&  p = pts.pts[i];
				const Vec   q = other.chain ? Closest(p, other.c, a_term.lo, a_term.hi) : other.pts[0];
				const float d = (p - q).Length();
				const Pair  pair{ ptsActor, a_moves[ptsActor].Inv(p), otherActor, a_moves[otherActor].Inv(q), a_term.gap, a_weight };
				if (closer) {
					if (d < bestD) {
						bestD = d;
						bestPair = pair;
					}
					continue;
				}
				if (a_dists) {
					a_dists->push_back(std::max(d - a_term.gap, 0.0f));
				}
				if (a_pairs) {
					a_pairs->push_back(pair);
				}
			}
			if (closer && pts.count > 0) {
				if (a_dists) {
					a_dists->push_back(std::max(bestD - a_term.gap, 0.0f));
				}
				if (a_pairs) {
					a_pairs->push_back(bestPair);
				}
			}
		}

		float Median(std::vector<float> a_v)
		{
			if (a_v.empty()) {
				return 0.0f;
			}
			const auto mid = a_v.begin() + a_v.size() / 2;
			std::nth_element(a_v.begin(), mid, a_v.end());
			return *mid;
		}

		// Gaussian elimination on an n x (n+1) augmented matrix. False if singular.
		bool SolveLinear(std::vector<std::vector<double>>& m, int n)
		{
			for (int c = 0; c < n; ++c) {
				int piv = c;
				for (int r = c + 1; r < n; ++r) {
					if (std::abs(m[r][c]) > std::abs(m[piv][c])) {
						piv = r;
					}
				}
				if (std::abs(m[piv][c]) < 1e-12) {
					return false;
				}
				std::swap(m[c], m[piv]);
				for (int r = 0; r < n; ++r) {
					if (r != c) {
						const double f = m[r][c] / m[c][c];
						for (int k = c; k <= n; ++k) {
							m[r][k] -= f * m[c][k];
						}
					}
				}
			}
			for (int c = 0; c < n; ++c) {
				m[c][n] /= m[c][c];
			}
			return true;
		}

		struct Eval
		{
			float       cost = FLT_MAX;
			std::string detail;
		};

		// How visible a change is: linear up to a_soft, then growing with the square (2 units of
		// floating are hard to see, 6 are obvious).
		float Visible(float a_amount, float a_soft)
		{
			a_amount = std::abs(a_amount);
			return a_amount <= a_soft ? a_amount : (a_amount * a_amount / a_soft + a_soft) * 0.5f;
		}

		// The direction the shaft should have for the main action: along the vaginal/anal canal
		// (entrance -> deep bone), straight into the mouth (upper lip -> back of the mouth), or through
		// the fist (along the knuckle line, either way). Median angle in degrees over the frames, or -1
		// if the action has no such direction. Moving an actor doesn't change it; the bend does.
		float DirectionAngle(const Context& a_ctx, const Term& a_term, const std::vector<int>& a_bends, Hands a_hands)
		{
			if ((a_term.partA == Part::Shaft) == (a_term.partB == Part::Shaft)) {
				return -1.0f;
			}
			const bool shaftIsA = a_term.partA == Part::Shaft;
			const int  shaftActor = shaftIsA ? a_term.a : a_term.b;
			const int  other = shaftIsA ? a_term.b : a_term.a;
			const Part part = shaftIsA ? a_term.partB : a_term.partA;
			if (part != Part::Vagina && part != Part::Anus && part != Part::Mouth && part != Part::Hand) {
				return -1.0f;
			}
			const auto angle = [](Vec a, Vec b, bool a_eitherWay) {
				if (a.Unitize() <= 0.0f || b.Unitize() <= 0.0f) {
					return -1.0f;
				}
				const float c = a_eitherWay ? std::abs(a.Dot(b)) : a.Dot(b);
				return std::acos(std::clamp(c, -1.0f, 1.0f)) * 57.29578f;
			};
			std::vector<float> out;
			for (std::size_t f = 0; f < a_ctx.in->frames.size(); ++f) {
				const auto  c = ShaftOf(a_ctx, f, shaftActor, a_bends[shaftActor]);
				const Vec   shaft = c[6] - c[2];
				const auto& p = a_ctx.in->frames[f][other];
				float       a = -1.0f;
				switch (part) {
				case Part::Vagina:
					a = angle(shaft, p.P(VaginaDeep1) - p.P(VaginaB1), false);
					break;
				case Part::Anus:
					a = angle(shaft, p.P(AnusDeep1) - p.P(Anus00), false);
					break;
				case Part::Mouth:
					a = angle(shaft, p.P(Mouth00) - MouthCentre(p), false);
					break;
				case Part::Hand:
					{
						const float r = angle(shaft, p.P(RF31) - p.P(RF11), true);
						const float l = angle(shaft, p.P(LF31) - p.P(LF11), true);
						a = a_hands == Hands::Right ? r : a_hands == Hands::Left ? l : (r + l) * 0.5f;
					}
					break;
				default:
					break;
				}
				if (a >= 0.0f) {
					out.push_back(a);
				}
			}
			return out.empty() ? -1.0f : Median(out);
		}

		// Cost of the terms for a set of moves/bend/hand choice, without the move penalties.
		Eval Evaluate(const Context& a_ctx, const Moves& a_moves, Hands a_hands, const std::vector<int>& a_bends)
		{
			const auto& st = Settings::Get();
			const auto  nf = a_ctx.in->frames.size();
			Eval        e;
			e.cost = 0.0f;
			for (std::size_t ti = 0; ti < a_ctx.terms.size(); ++ti) {
				const auto& term = a_ctx.terms[ti];
				const bool  primary = ti == 0;
				const bool  paired = Paired(term.partA) || Paired(term.partB);
				float       termCost = 0.0f;
				std::string parts;
				if (primary && paired) {
					// both sides always count: a hand/foot that can't reach is busy elsewhere
					for (const auto side : { Hands::Right, Hands::Left }) {
						std::vector<float> d;
						for (std::size_t f = 0; f < nf; ++f) {
							TermFrame(a_ctx, term, f, a_moves, side, a_bends, &d, nullptr, 1.0f);
						}
						const float med = Median(d);
						const bool  chosen = a_hands == Hands::Both || a_hands == side;
						termCost += chosen ? Visible(med, 2.0f) : std::min(med, st.engagedDistance);
						parts += std::format("{}{} {:.1f} ", side == Hands::Right ? "R" : "L", chosen ? "*" : "", med);
					}
				} else {
					std::vector<float> d;
					for (std::size_t f = 0; f < nf; ++f) {
						TermFrame(a_ctx, term, f, a_moves, paired ? Hands::Closer : Hands::Both, a_bends, &d, nullptr, 1.0f);
					}
					const float med = Median(d);
					termCost = primary ? Visible(med, 2.0f) : std::min(med, st.engagedDistance);
					parts = std::format("{:.1f} ", med);
				}
				if (primary) {
					if (const float a = DirectionAngle(a_ctx, term, a_bends, a_hands); a >= 0.0f) {
						termCost += std::max(a - st.angleDeadzone, 0.0f) / st.anglePerUnit;
						parts += std::format("angle {:.0f} ", a);
					}
				}
				e.cost += termCost * term.weight;
				e.detail += std::format("{}: {}| ", term.action, parts);
			}
			return e;
		}

		// Least-squares fit of scale + offset for the actors in a_movers, iterating closest points
		// (ICP). Several movers share the correction according to their costs.
		Moves Fit(const Context& a_ctx, const std::vector<int>& a_movers, Hands a_hands, const std::vector<int>& a_bends)
		{
			const auto& st = Settings::Get();
			Moves       moves(a_ctx.in->actors->size());
			for (std::size_t i = 0; i < moves.size(); ++i) {
				moves[i].r = (*a_ctx.in->actors)[i].root;
			}
			const int  per = st.adjustScale ? 4 : 3;  // unknowns per mover: [s,] tx, ty, tz
			const int  n = per * static_cast<int>(a_movers.size());
			const auto slot = [&](int a_actor) {
				const auto it = std::ranges::find(a_movers, a_actor);
				return it == a_movers.end() ? -1 : static_cast<int>(it - a_movers.begin());
			};
			std::vector<double> row(n);
			std::vector<double> heightWeight(a_movers.size(), st.costHeight);  // costSink once a mover goes down
			for (int iter = 0; iter < 12; ++iter) {
				std::vector<Pair> pairs;
				for (std::size_t ti = 0; ti < a_ctx.terms.size(); ++ti) {
					const auto& term = a_ctx.terms[ti];
					if (slot(term.a) < 0 && slot(term.b) < 0) {
						continue;
					}
					const bool         primary = ti == 0;
					const bool         paired = Paired(term.partA) || Paired(term.partB);
					const auto         hands = primary ? a_hands : (paired ? Hands::Closer : Hands::Both);
					std::vector<float> d;
					const auto         before = pairs.size();
					for (std::size_t f = 0; f < a_ctx.in->frames.size(); ++f) {
						TermFrame(a_ctx, term, f, moves, hands, a_bends, &d, &pairs, term.weight);
					}
					if (!primary && iter > 0) {
						// a secondary part that stays far away is busy elsewhere: don't let it pull
						for (std::size_t i = before; i < pairs.size(); ++i) {
							if (d[i - before] > st.engagedDistance) {
								pairs[i].weight = 0.0f;
							}
						}
					}
					if (primary && !d.empty()) {
						// frames far off the typical one (a glitch while sampling, a wild swing) don't pull
						const float limit = std::max(3.0f * Median(d), st.engagedDistance);
						for (std::size_t i = before; i < pairs.size(); ++i) {
							if (d[i - before] > limit) {
								pairs[i].weight = 0.0f;
							}
						}
					}
				}
				std::vector<std::vector<double>> m(n, std::vector<double>(n + 1, 0.0));
				double                           wsum = 0.0;
				const auto                       add = [&](double a_rhs, double a_w) {
                    for (int i = 0; i < n; ++i) {
                        if (row[i] == 0.0) {
                            continue;
                        }
                        for (int j = 0; j < n; ++j) {
                            m[i][j] += a_w * row[i] * row[j];
                        }
                        m[i][n] += a_w * row[i] * a_rhs;
                    }
				};
				for (const auto& pr : pairs) {
					if (pr.weight <= 0.0f) {
						continue;
					}
					wsum += pr.weight;
					// Fwd_P(p) - Fwd_Q(q) should equal the contact gap, along their current difference
					const Vec   pNow = moves[pr.actorP].Fwd(pr.p);
					const Vec   qNow = moves[pr.actorQ].Fwd(pr.q);
					Vec         dir = pNow - qNow;
					const float dist = dir.Unitize();
					const Vec   gap = dir * std::min(pr.gap, dist);
					const int   sp = slot(pr.actorP);
					const int   sq = slot(pr.actorQ);
					const Vec&  rp = moves[pr.actorP].r;
					const Vec&  rq = moves[pr.actorQ].r;
					const Vec   dp = pr.p - rp;
					const Vec   dq = pr.q - rq;
					const float gapc[3] = { gap.x, gap.y, gap.z };
					const float dpc[3] = { dp.x, dp.y, dp.z };
					const float dqc[3] = { dq.x, dq.y, dq.z };
					const float rpc[3] = { rp.x, rp.y, rp.z };
					const float rqc[3] = { rq.x, rq.y, rq.z };
					const float pc[3] = { pr.p.x, pr.p.y, pr.p.z };
					const float qc[3] = { pr.q.x, pr.q.y, pr.q.z };
					for (int c = 0; c < 3; ++c) {
						std::ranges::fill(row, 0.0);
						double rhs = gapc[c];
						if (sp >= 0 && per == 4) {
							row[sp * per] += dpc[c];
							row[sp * per + 1 + c] += 1.0;
							rhs -= rpc[c];
						} else if (sp >= 0) {
							row[sp * per + c] += 1.0;
							rhs -= pc[c];
						} else {
							rhs -= pc[c];
						}
						if (sq >= 0 && per == 4) {
							row[sq * per] -= dqc[c];
							row[sq * per + 1 + c] -= 1.0;
							rhs += rqc[c];
						} else if (sq >= 0) {
							row[sq * per + c] -= 1.0;
							rhs += qc[c];
						} else {
							rhs += qc[c];
						}
						add(rhs, pr.weight);
					}
				}
				if (wsum <= 0.0) {
					break;
				}
				// regularisation: squared penalties, weighted like one pair per frame
				const double reg = wsum / 3.0;
				const double ws = st.costScalePercent * 100.0, wxy = st.costSlide;
				const auto   unit = [&](int a_index, double a_w, double a_target) {
                    std::ranges::fill(row, 0.0);
                    row[a_index] = a_w;
                    add(a_w * a_target, reg);
				};
				for (int k = 0; k < static_cast<int>(a_movers.size()); ++k) {
					const int    o = k * per;
					const double wz = heightWeight[k];
					if (per == 4) {
						unit(o, ws, 1.0);
						unit(o + 1, wxy, 0.0);
						unit(o + 2, wxy, 0.0);
						unit(o + 3, wz, 0.0);
					} else {
						unit(o, wxy, 0.0);
						unit(o + 1, wxy, 0.0);
						unit(o + 2, wz, 0.0);
					}
				}
				if (!SolveLinear(m, n)) {
					break;
				}
				bool done = true;
				for (int k = 0; k < static_cast<int>(a_movers.size()); ++k) {
					auto&     xf = moves[a_movers[k]];
					Xf        next = xf;
					const int o = k * per;
					if (per == 4) {
						next.s = std::clamp(static_cast<float>(m[o][n]), st.scaleMin, st.scaleMax);
						next.t = { static_cast<float>(m[o + 1][n]), static_cast<float>(m[o + 2][n]), static_cast<float>(m[o + 3][n]) };
					} else {
						next.t = { static_cast<float>(m[o][n]), static_cast<float>(m[o + 1][n]), static_cast<float>(m[o + 2][n]) };
					}
					if (const float len = next.t.Length(); len > st.maxOffset) {
						next.t *= st.maxOffset / len;
					}
					done = done && std::abs(next.s - xf.s) < 1e-4f && (next.t - xf.t).Length() < 0.02f;
					xf = next;
					heightWeight[k] = xf.t.z < 0.0f ? st.costSink : st.costHeight;
				}
				if (done) {
					break;
				}
			}
			return moves;
		}

		float Penalty(const Moves& a_moves)
		{
			const auto& st = Settings::Get();
			float       sum = 0.0f;
			for (const auto& xf : a_moves) {
				const float height = xf.t.z >= 0.0f ? st.costHeight * Visible(xf.t.z, 3.0f) : st.costSink * Visible(xf.t.z, 2.0f);
				sum += height + st.costScalePercent * Visible(100.0f * (xf.s - 1.0f), 3.0f) +
				       st.costSlide * std::sqrt(xf.t.x * xf.t.x + xf.t.y * xf.t.y);
			}
			return sum;
		}

		const char* HandsName(Hands h)
		{
			switch (h) {
			case Hands::Both:
				return "both";
			case Hands::Right:
				return "R";
			case Hands::Left:
				return "L";
			default:
				return "closer";
			}
		}

		std::optional<Context> MakeContext(const Input& a_in)
		{
			Context ctx;
			ctx.in = &a_in;
			ctx.models.resize(a_in.actors->size());
			for (const auto& term : a_in.terms) {
				for (const auto& [idx, part] : { std::pair{ term.a, term.partA }, std::pair{ term.b, term.partB } }) {
					if (part != Part::Shaft || ctx.models[idx]) {
						continue;
					}
					std::vector<Pose> poses;
					for (const auto& f : a_in.frames) {
						poses.push_back(f[idx]);
					}
					ctx.models[idx] = FitShaft(poses, (*a_in.actors)[idx], a_in.bends[idx]);
				}
			}
			for (const auto& term : a_in.terms) {
				const auto& actors = *a_in.actors;
				const bool  okA = HasPart(actors[term.a], term.partA) && (term.partA != Part::Shaft || ctx.models[term.a]);
				const bool  okB = HasPart(actors[term.b], term.partB) && (term.partB != Part::Shaft || ctx.models[term.b]);
				if (okA && okB) {
					ctx.terms.push_back(term);
				} else if (ctx.terms.empty()) {
					return std::nullopt;  // the main action can't be measured on these bodies
				}
			}
			if (ctx.terms.empty()) {
				return std::nullopt;
			}
			const auto& p = ctx.terms.front();
			ctx.bendActor = p.partA == Part::Shaft ? p.a : (p.partB == Part::Shaft ? p.b : -1);
			return ctx;
		}
	}

	std::vector<Term> TermsFor(const Scenes::Scene& a_scene, int a_actorCount)
	{
		std::vector<Term> terms;
		for (const auto& action : a_scene.actions) {
			const auto def = std::ranges::find_if(kDefs, [&](const ActionDef& d) { return action.type == d.name; });
			if (def == std::end(kDefs) || action.actor == action.target || action.actor < 0 || action.target < 0 ||
				action.actor >= a_actorCount || action.target >= a_actorCount) {
				continue;
			}
			Term t;
			t.action = def->name;
			t.partA = def->a;
			t.partB = def->b;
			t.a = action.actor;
			t.b = action.target;
			t.lo = def->lo;
			t.hi = def->hi;
			t.gap = def->gap;
			t.priority = def->priority;
			terms.push_back(t);
		}
		std::ranges::stable_sort(terms, [](const Term& l, const Term& r) { return l.priority > r.priority; });
		if (terms.empty()) {
			return terms;
		}
		// keep the main action and the other ones between the same two actors
		const auto        main = terms.front();
		std::vector<Term> out{ main };
		for (std::size_t i = 1; i < terms.size(); ++i) {
			const auto& t = terms[i];
			const bool  samePair = (t.a == main.a && t.b == main.b) || (t.a == main.b && t.b == main.a);
			const bool  duplicate = std::ranges::any_of(out, [&](const Term& o) {
                return o.partA == t.partA && o.partB == t.partB && ((o.a == t.a && o.b == t.b) || (o.a == t.b && o.b == t.a));
            });
			if (samePair && !duplicate) {
				out.push_back(t);
				out.back().weight = Settings::Get().secondaryWeight;
			}
		}
		return out;
	}

	Plan Solve(const Input& a_in)
	{
		const auto& st = Settings::Get();
		Plan        plan;
		auto        ctxOpt = MakeContext(a_in);
		if (!ctxOpt || a_in.frames.empty()) {
			return plan;
		}
		const auto& ctx = *ctxOpt;
		const auto& main = ctx.terms.front();
		const bool  paired = Paired(main.partA) || Paired(main.partB);
		const std::vector<Hands> handOptions = paired ? std::vector{ Hands::Both, Hands::Right, Hands::Left } : std::vector{ Hands::Both };

		Moves still(a_in.actors->size());
		for (std::size_t i = 0; i < still.size(); ++i) {
			still[i].r = (*a_in.actors)[i].root;
		}
		// leaving things as they are
		{
			Eval best;
			for (const auto h : handOptions) {
				const auto e = Evaluate(ctx, still, h, a_in.bends);
				if (e.cost < best.cost) {
					best = e;
				}
			}
			plan.baseline = best.cost;
			plan.baselineDetail = best.detail;
		}

		std::vector<int> bendOptions{ ctx.bendActor >= 0 ? a_in.bends[ctx.bendActor] : 0 };
		if (ctx.bendActor >= 0 && st.adjustBend) {
			bendOptions.clear();
			for (int b = -9; b <= 9; ++b) {
				bendOptions.push_back(b);
			}
		}
		// move the one actor, the other, or both (sharing the change)
		std::vector<std::vector<int>> moverSets{ { main.a } };
		if (main.b != main.a) {
			moverSets.push_back({ main.b });
			moverSets.push_back({ main.a, main.b });
		}
		const auto describe = [&](const Moves& a_moves) {
			std::string out;
			for (std::size_t i = 0; i < a_moves.size(); ++i) {
				if (a_moves[i].Moved()) {
					out += std::format("actor {} by ({:.2f}, {:.2f}, {:.2f}) scale x{:.3f}; ", i, a_moves[i].t.x, a_moves[i].t.y, a_moves[i].t.z, a_moves[i].s);
				}
			}
			return out.empty() ? "nothing; "s : out;
		};

		float bestCost = FLT_MAX;
		float keepCost = FLT_MAX;
		for (const int bend : bendOptions) {
			auto bends = a_in.bends;
			if (ctx.bendActor >= 0) {
				bends[ctx.bendActor] = bend;
			}
			const float bendCost = ctx.bendActor >= 0 ? st.costBendStep * std::abs(bend - a_in.bends[ctx.bendActor]) : 0.0f;
			for (const auto h : handOptions) {
				// changing only the bend (or nothing) is always a candidate
				{
					const auto  e = Evaluate(ctx, still, h, bends);
					const float cost = e.cost + bendCost;
					if (ctx.bendActor >= 0 && bend == a_in.bends[ctx.bendActor] && cost < keepCost) {
						keepCost = cost;
						plan.keepBend = std::format("move nothing; cost {:.2f}: {}", cost, e.detail);
					}
					if (cost < bestCost) {
						bestCost = cost;
						plan.valid = true;
						plan.moves.clear();
						plan.bendActor = ctx.bendActor;
						plan.bend = bend;
						plan.hands = paired ? HandsName(h) : "";
						plan.cost = cost;
						plan.predicted = e.cost;
						plan.detail = e.detail;
						plan.summary = "nothing; ";
					}
				}
				for (const auto& movers : moverSets) {
					const auto  moves = Fit(ctx, movers, h, bends);
					const auto  e = Evaluate(ctx, moves, h, bends);
					const float cost = e.cost + Penalty(moves) + bendCost;
					if (ctx.bendActor >= 0 && bend == a_in.bends[ctx.bendActor] && cost < keepCost) {
						keepCost = cost;
						plan.keepBend = std::format("move {}cost {:.2f}: {}", describe(moves), cost, e.detail);
					}
					if (cost < bestCost) {
						bestCost = cost;
						plan.valid = true;
						plan.moves.clear();
						for (const int m : movers) {
							if (moves[m].Moved()) {
								plan.moves.push_back({ m, moves[m].s, moves[m].t });
							}
						}
						plan.bendActor = ctx.bendActor;
						plan.bend = bend;
						plan.hands = paired ? HandsName(h) : "";
						plan.cost = cost;
						plan.predicted = e.cost;
						plan.detail = e.detail;
						plan.summary = describe(moves);
					}
				}
			}
		}
		return plan;
	}

	float Measure(const Input& a_in, std::string& a_detail)
	{
		auto ctx = MakeContext(a_in);
		if (!ctx) {
			return -1.0f;
		}
		const auto& main = ctx->terms.front();
		const bool  paired = Paired(main.partA) || Paired(main.partB);
		Moves       still(a_in.actors->size());
		for (std::size_t i = 0; i < still.size(); ++i) {
			still[i].r = (*a_in.actors)[i].root;
		}
		Eval best;
		for (const auto h : paired ? std::vector{ Hands::Both, Hands::Right, Hands::Left } : std::vector{ Hands::Both }) {
			const auto e = Evaluate(*ctx, still, h, a_in.bends);
			if (e.cost < best.cost) {
				best = e;
			}
		}
		a_detail = best.detail;
		return best.cost;
	}
}
