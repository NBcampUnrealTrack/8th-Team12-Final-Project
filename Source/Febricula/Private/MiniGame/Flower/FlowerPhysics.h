// [skydodo] 2026-10-12 작성
// 요약: 꽃집 미니게임 물리 - 꽃 모양대로 충돌하는 2D 강체 (원 여러 개를 묶은 모양 · 회전 · 마찰)
// Fill out your copyright notice in the Description page of Project Settings.
//
// 처음엔 HTML처럼 꽃을 원 하나로 다뤘더니 깔끔하게만 쌓이고 무너지는 맛이 없었다.
// 꽃 그림 실루엣에 맞춰 원 몇 개를 붙인 강체로 바꿨다 (튤립은 봉오리 + 양쪽 잎, 꽃다발은 고깔 …).
// 꽃은 회전하고 마찰이 있어서 비스듬히 걸치고, 엇갈려 쌓이다 무너진다.
//
// 언리얼 타입을 쓰지 않는 순수 C++ 헤더다. 난이도 시뮬레이터(에디터 밖 콘솔 프로그램)가 같은 파일을 그대로 쓴다.
// 크기를 고정한 배열만 쓴다 (꽃 최대 128개).
//
// 한 스텝 순서 (작은 강체 엔진의 기본형):
//   중력 → 접촉 만들기(+ 같은 꽃 합성) → 충돌 · 마찰 충격량 반복 → 위치 이동 → 겹침 밀어내기

#pragma once

#include <cmath>

namespace FlowerPhysics
{
	constexpr int NumTiers = 8;
	constexpr int MaxTier = NumTiers - 1;
	constexpr int MaxBodies = 128;
	constexpr int MaxCircles = 5;
	constexpr int MaxContacts = 1024;
	constexpr int MaxEvents = 64;

	struct FCircle
	{
		float X;
		float Y;
		float R;
	};

	/** 단계별 모양. 원 좌표 · 반지름은 그림 크기(Size)에 대한 비율, 그림 가운데 기준 */
	struct FTierShapeDesc
	{
		float Size;
		int Count;
		FCircle Circles[MaxCircles];
	};

	/**
	 * 그림 실루엣에서 자동으로 뽑은 원(가장 큰 내접원부터)을 바탕으로, 잎 · 줄기 같은 돌출부를 살려 손으로 정리했다.
	 * Size는 화면(840×1240 캔버스) 위 그림 크기. HTML 원본보다 1.4배쯤 키워 작업대에 압박이 생기게 했다.
	 */
	inline const FTierShapeDesc* GetTierShapeDescs()
	{
		static const FTierShapeDesc Descs[NumTiers] =
		{
			// 씨앗: 통통한 몸통 + 위로 솟은 떡잎
			{ 76.f, 3, { { -0.03f, 0.11f, 0.28f }, { 0.18f, -0.26f, 0.16f }, { -0.25f, 0.28f, 0.10f } } },
			// 새싹: 양쪽 잎 + 줄기 (V자)
			{ 100.f, 4, { { -0.18f, -0.12f, 0.18f }, { 0.215f, -0.11f, 0.18f }, { 0.f, 0.01f, 0.135f }, { -0.02f, 0.21f, 0.12f } } },
			// 데이지: 꽃송이 + 양쪽 아래 잎
			{ 132.f, 3, { { 0.016f, 0.02f, 0.347f }, { -0.31f, 0.145f, 0.095f }, { 0.35f, 0.125f, 0.065f } } },
			// 튤립: 봉오리 + 넓게 벌어진 잎 + 줄기 (부채꼴)
			{ 170.f, 4, { { 0.f, -0.06f, 0.31f }, { -0.28f, 0.22f, 0.12f }, { 0.28f, 0.22f, 0.12f }, { 0.f, 0.30f, 0.10f } } },
			// 장미: 꽃송이 + 아래 양쪽 잎
			{ 210.f, 3, { { 0.004f, -0.004f, 0.379f }, { -0.31f, 0.215f, 0.097f }, { 0.30f, 0.22f, 0.09f } } },
			// 해바라기: 큰 꽃송이 + 아래 잎
			{ 256.f, 3, { { 0.02f, 0.008f, 0.38f }, { -0.30f, 0.21f, 0.08f }, { 0.324f, 0.238f, 0.06f } } },
			// 수국: 울퉁불퉁한 꽃 무더기 + 잎
			{ 306.f, 5, { { -0.13f, -0.12f, 0.25f }, { 0.15f, -0.10f, 0.24f }, { 0.f, 0.12f, 0.27f }, { 0.227f, 0.242f, 0.10f }, { -0.324f, 0.168f, 0.084f } } },
			// 꽃다발: 위가 넓고 아래가 좁은 고깔
			{ 360.f, 5, { { 0.f, -0.12f, 0.30f }, { -0.25f, -0.06f, 0.15f }, { 0.27f, -0.12f, 0.14f }, { 0.f, 0.14f, 0.20f }, { 0.06f, 0.30f, 0.09f } } },
		};
		return Descs;
	}

	/** 계산해 둔 단계별 모양 (원 좌표는 무게중심 기준 px) */
	struct FTierShape
	{
		float Size = 0.f;
		int Count = 0;
		FCircle Circles[MaxCircles] = {};
		float InvMass = 0.f;
		float InvInertia = 0.f;
		/** 무게중심에서 가장 먼 원 끝까지 (대략적인 판정 범위) */
		float BoundR = 0.f;
		/** 회전 0일 때 무게중심에서 좌우 끝까지 */
		float HalfWidth = 0.f;
		/** 무게중심 → 그림 가운데 */
		float ArtX = 0.f;
		float ArtY = 0.f;
		/** 같은 넓이 원의 반지름 (효과 크기 · 위험선 표시 등) */
		float EquivR = 0.f;
	};

	struct FBody
	{
		int Id = 0;
		int Tier = 0;
		float X = 0.f;
		float Y = 0.f;
		float A = 0.f;
		float VX = 0.f;
		float VY = 0.f;
		float W = 0.f;
		float Age = 0.f;
		/** 처음 어딘가에 닿았는지 - 착지 효과는 한 번만 */
		bool bLanded = false;
		// ---- 연출용 (물리에는 안 쓴다) ----
		float Pop = 0.f;
		float Squash = 0.f;
		float Wobble = 0.f;
		float TrailTimer = 0.f;
	};

	struct FParams
	{
		float Left = 45.f;
		float Right = 795.f;
		float Floor = 1140.f;
		float Gravity = 1600.f;
		float LinearDamping = 0.6f;
		float AngularDamping = 1.2f;
		float Friction = 0.45f;
		float WallFriction = 0.2f;
		/** 이 속도보다 세게 부딪히면 튕긴다 (살살 닿은 건 그대로 쌓이게) */
		float Restitution = 0.22f;
		float BounceMinSpeed = 220.f;
		/** 같은 꽃 원끼리 이 배율 거리 안이면 합성 */
		float MergeReach = 1.05f;
		float MergeGrace = 0.08f;
		int Iterations = 10;
		/** 겹침 밀어내기 (남은 겹침의 비율, 허용 겹침 px) */
		float PositionFix = 0.6f;
		float Slop = 0.5f;
		/** 착지 이벤트를 낼 최소 충돌 속도 */
		float LandImpactMin = 120.f;
	};

	enum class EEventType : unsigned char
	{
		Landed,
		Merged,
	};

	struct FEvent
	{
		EEventType Type = EEventType::Landed;
		/** Landed: 착지한 꽃 Id. Merged: 새로 생긴 꽃 Id (꽃다발 둘이 합쳐져 사라졌으면 -1) */
		int Id = -1;
		/** Merged: 새 단계 (꽃다발 둘이면 NumTiers) */
		int Tier = 0;
		float X = 0.f;
		float Y = 0.f;
		float Impact = 0.f;
		/** Merged: 합쳐진 두 꽃의 평균 각도 */
		float Angle = 0.f;
	};

	struct FContact
	{
		int A;          // 몸 번호. 벽 · 바닥이면 B = -1
		int B;
		float NX;       // A → B 방향
		float NY;
		float Pen;
		float PX;       // 접촉점 (월드)
		float PY;
	};

	class FWorld
	{
	public:
		/** 벽 · 바닥 · 마찰 등 시뮬레이션 설정. 위젯이 시작할 때 채운다 */
		FParams P;

		int GetCount() const { return Count; }
		FBody& GetBody(int Index) { return Bodies[Index]; }
		const FBody& GetBody(int Index) const { return Bodies[Index]; }
		const FTierShape& GetShape(int Tier) const { return Shapes[Tier]; }

		/** 지난 Step 이후 쌓인 착지 · 합체 이벤트. 처리한 뒤 ClearEvents로 비운다 */
		int GetNumEvents() const { return NumEvents; }
		const FEvent& GetEvent(int Index) const { return Events[Index]; }
		void ClearEvents() { NumEvents = 0; }

	private:
		FTierShape Shapes[NumTiers];
		FBody Bodies[MaxBodies];
		int Count = 0;
		int Serial = 0;
		FEvent Events[MaxEvents];
		int NumEvents = 0;

	public:
		FWorld()
		{
			BuildShapes();
		}

		void BuildShapes()
		{
			const FTierShapeDesc* Descs = GetTierShapeDescs();
			for (int T = 0; T < NumTiers; ++T)
			{
				const FTierShapeDesc& D = Descs[T];
				FTierShape& S = Shapes[T];
				S.Size = D.Size;
				S.Count = D.Count;

				// 무게중심 (원 넓이 비례)
				float Mass = 0.f, Cx = 0.f, Cy = 0.f;
				for (int i = 0; i < D.Count; ++i)
				{
					const float R = D.Circles[i].R * D.Size;
					const float M = R * R;
					Mass += M;
					Cx += D.Circles[i].X * D.Size * M;
					Cy += D.Circles[i].Y * D.Size * M;
				}
				Cx /= Mass;
				Cy /= Mass;

				float Inertia = 0.f;
				S.BoundR = 0.f;
				S.HalfWidth = 0.f;
				for (int i = 0; i < D.Count; ++i)
				{
					FCircle& C = S.Circles[i];
					C.X = D.Circles[i].X * D.Size - Cx;
					C.Y = D.Circles[i].Y * D.Size - Cy;
					C.R = D.Circles[i].R * D.Size;
					const float M = C.R * C.R;
					Inertia += M * (C.R * C.R * 0.5f + C.X * C.X + C.Y * C.Y);
					S.BoundR = std::fmax(S.BoundR, std::sqrt(C.X * C.X + C.Y * C.Y) + C.R);
					S.HalfWidth = std::fmax(S.HalfWidth, std::fabs(C.X) + C.R);
				}
				// 질량 단위는 아무거나 상관없다 (비율만 쓴다). 큰 꽃이 무겁게 넓이 비례
				const float Unit = 1.f / 1000.f;
				S.InvMass = 1.f / (Mass * Unit);
				S.InvInertia = 1.f / (Inertia * Unit);
				S.ArtX = -Cx;
				S.ArtY = -Cy;
				S.EquivR = std::sqrt(Mass);
			}
		}

		void Reset()
		{
			Count = 0;
			Serial = 0;
			NumEvents = 0;
		}

		FBody* FindById(int Id)
		{
			for (int i = 0; i < Count; ++i)
			{
				if (Bodies[i].Id == Id)
				{
					return &Bodies[i];
				}
			}
			return nullptr;
		}

		/** 새 꽃. 꽉 찼으면 -1 */
		int Add(int Tier, float X, float Y, float Angle, float VY)
		{
			if (Count >= MaxBodies)
			{
				return -1;
			}
			FBody& B = Bodies[Count++];
			B = FBody();
			B.Id = Serial++;
			B.Tier = Tier;
			B.X = X;
			B.Y = Y;
			B.A = Angle;
			B.VY = VY;
			return B.Id;
		}

		void RemoveAt(int Index)
		{
			// 순서를 지킨다 (그리는 순서가 바뀌면 깜빡여 보인다)
			for (int i = Index; i < Count - 1; ++i)
			{
				Bodies[i] = Bodies[i + 1];
			}
			--Count;
		}

		/** 원 i의 월드 좌표 */
		void CircleWorld(const FBody& B, int i, float& OutX, float& OutY) const
		{
			const FCircle& C = Shapes[B.Tier].Circles[i];
			const float Cs = std::cos(B.A), Sn = std::sin(B.A);
			OutX = B.X + C.X * Cs - C.Y * Sn;
			OutY = B.Y + C.X * Sn + C.Y * Cs;
		}

		/** 가장 위에 있는 원의 윗끝 y (위험선 판정) */
		float TopY(const FBody& B) const
		{
			float Top = 1e9f;
			for (int i = 0; i < Shapes[B.Tier].Count; ++i)
			{
				float X, Y;
				CircleWorld(B, i, X, Y);
				Top = std::fmin(Top, Y - Shapes[B.Tier].Circles[i].R);
			}
			return Top;
		}

		/** (X, Y) 주변 꽃을 바깥으로 밀어낸다 (살짝 위로 띄워 들썩이게) */
		void Push(float X, float Y, float Radius, float Strength, int ExcludeId)
		{
			for (int i = 0; i < Count; ++i)
			{
				FBody& B = Bodies[i];
				if (B.Id == ExcludeId)
				{
					continue;
				}
				const float Dx = B.X - X, Dy = B.Y - Y;
				const float D = std::sqrt(Dx * Dx + Dy * Dy);
				const float Reach = Radius + Shapes[B.Tier].BoundR;
				if (D > Reach || D < 0.001f)
				{
					continue;
				}
				const float Falloff = 1.f - D / Reach;
				B.VX += Dx / D * Strength * Falloff;
				B.VY += Dy / D * Strength * Falloff - Strength * 0.35f * Falloff;
				// 밀리면서 살짝 돈다
				B.W += (Dx >= 0.f ? 1.f : -1.f) * Strength * Falloff * 0.004f;
			}
		}

		void Step(float Dt)
		{
			// 1) 중력 · 감쇠
			const float LinDrag = std::exp(-P.LinearDamping * Dt);
			const float AngDrag = std::exp(-P.AngularDamping * Dt);
			for (int i = 0; i < Count; ++i)
			{
				FBody& B = Bodies[i];
				B.Age += Dt;
				B.VY += P.Gravity * Dt;
				B.VX *= LinDrag;
				// 공중에서는 빙글 도는 채로 떨어지게, 회전 감쇠는 닿은 뒤에만
				if (B.bLanded)
				{
					B.W *= AngDrag;
				}
			}

			// 2) 접촉 (같은 꽃이 닿으면 합성하고 접촉을 처음부터 다시 만든다)
			int NumContacts = 0;
			for (int Guard = 0; Guard < 8; ++Guard)
			{
				int MergeA = -1, MergeB = -1;
				NumContacts = BuildContacts(MergeA, MergeB);
				if (MergeA < 0)
				{
					break;
				}
				Merge(MergeA, MergeB);
			}

			// 3) 충돌 · 마찰 충격량
			for (int It = 0; It < P.Iterations; ++It)
			{
				for (int c = 0; c < NumContacts; ++c)
				{
					SolveVelocity(Contacts[c], It == 0);
				}
			}

			// 4) 위치 이동
			for (int i = 0; i < Count; ++i)
			{
				FBody& B = Bodies[i];
				B.X += B.VX * Dt;
				B.Y += B.VY * Dt;
				B.A += B.W * Dt;
			}

			// 5) 남은 겹침 밀어내기
			for (int c = 0; c < NumContacts; ++c)
			{
				FixPosition(Contacts[c]);
			}
		}

	private:
		FContact Contacts[MaxContacts];

		void AddEvent(const FEvent& E)
		{
			if (NumEvents < MaxEvents)
			{
				Events[NumEvents++] = E;
			}
		}

		int BuildContacts(int& OutMergeA, int& OutMergeB)
		{
			int N = 0;
			for (int a = 0; a < Count; ++a)
			{
				const FBody& A = Bodies[a];
				const FTierShape& SA = Shapes[A.Tier];
				float AX[MaxCircles], AY[MaxCircles];
				for (int i = 0; i < SA.Count; ++i)
				{
					CircleWorld(A, i, AX[i], AY[i]);

					// 벽 · 바닥
					const float R = SA.Circles[i].R;
					if (AX[i] - R < P.Left && N < MaxContacts)
					{
						Contacts[N++] = { a, -1, -1.f, 0.f, P.Left - (AX[i] - R), AX[i] - R, AY[i] };
					}
					if (AX[i] + R > P.Right && N < MaxContacts)
					{
						Contacts[N++] = { a, -1, 1.f, 0.f, AX[i] + R - P.Right, AX[i] + R, AY[i] };
					}
					if (AY[i] + R > P.Floor && N < MaxContacts)
					{
						Contacts[N++] = { a, -1, 0.f, 1.f, AY[i] + R - P.Floor, AX[i], AY[i] + R };
					}
				}

				for (int b = a + 1; b < Count; ++b)
				{
					const FBody& B = Bodies[b];
					const FTierShape& SB = Shapes[B.Tier];
					const float Bx = B.X - A.X, By = B.Y - A.Y;
					const float Reach = (SA.BoundR + SB.BoundR) * P.MergeReach;
					if (Bx * Bx + By * By > Reach * Reach)
					{
						continue;
					}
					const bool bCanMerge = A.Tier == B.Tier && A.Age > P.MergeGrace && B.Age > P.MergeGrace;
					for (int j = 0; j < SB.Count; ++j)
					{
						float BX, BY;
						CircleWorld(B, j, BX, BY);
						for (int i = 0; i < SA.Count; ++i)
						{
							const float Dx = BX - AX[i], Dy = BY - AY[i];
							const float Rr = SA.Circles[i].R + SB.Circles[j].R;
							const float D2 = Dx * Dx + Dy * Dy;
							if (bCanMerge && D2 <= Rr * Rr * P.MergeReach * P.MergeReach)
							{
								OutMergeA = a;
								OutMergeB = b;
								return N;
							}
							if (D2 >= Rr * Rr || N >= MaxContacts)
							{
								continue;
							}
							float D = std::sqrt(D2);
							float Nx = 1.f, Ny = 0.f;
							if (D > 0.0001f)
							{
								Nx = Dx / D;
								Ny = Dy / D;
							}
							const float Ra = SA.Circles[i].R;
							Contacts[N++] = { a, b, Nx, Ny, Rr - D, AX[i] + Nx * Ra, AY[i] + Ny * Ra };
						}
					}
				}
			}
			return N;
		}

		void Merge(int IndexA, int IndexB)
		{
			const FBody A = Bodies[IndexA];
			const FBody B = Bodies[IndexB];
			// 뒤 번호부터 지워야 앞 번호가 안 밀린다
			RemoveAt(IndexA > IndexB ? IndexA : IndexB);
			RemoveAt(IndexA > IndexB ? IndexB : IndexA);

			const int NewTier = A.Tier + 1;
			FEvent E;
			E.Type = EEventType::Merged;
			E.Tier = NewTier;
			E.X = (A.X + B.X) * 0.5f;
			E.Y = (A.Y + B.Y) * 0.5f;
			E.Angle = (A.A + B.A) * 0.5f;
			if (NewTier <= MaxTier)
			{
				const FTierShape& S = Shapes[NewTier];
				const float X = std::fmin(std::fmax(E.X, P.Left + S.HalfWidth), P.Right - S.HalfWidth);
				const float Y = std::fmin(E.Y, P.Floor - S.BoundR);
				E.Id = Add(NewTier, X, Y, E.Angle, -65.f);
				if (FBody* Born = FindById(E.Id))
				{
					Born->bLanded = true; // 피어난 자리에서 시작 - 착지 효과는 주지 않는다
					Born->VX = (A.VX + B.VX) * 0.5f;
					Born->W = (A.W + B.W) * 0.5f;
				}
			}
			AddEvent(E);
		}

		void NoteLanding(FBody& B, float Impact)
		{
			if (B.bLanded)
			{
				return;
			}
			B.bLanded = true;
			if (Impact >= P.LandImpactMin)
			{
				FEvent E;
				E.Type = EEventType::Landed;
				E.Id = B.Id;
				E.Tier = B.Tier;
				E.X = B.X;
				E.Y = B.Y;
				E.Impact = Impact;
				AddEvent(E);
			}
		}

		void SolveVelocity(const FContact& C, bool bFirst)
		{
			FBody& A = Bodies[C.A];
			const FTierShape& SA = Shapes[A.Tier];
			const float RaX = C.PX - A.X, RaY = C.PY - A.Y;
			// 접촉점 속도 = v + w × r
			float VaX = A.VX - A.W * RaY, VaY = A.VY + A.W * RaX;

			FBody* B = C.B >= 0 ? &Bodies[C.B] : nullptr;
			float RbX = 0.f, RbY = 0.f, VbX = 0.f, VbY = 0.f, InvMB = 0.f, InvIB = 0.f;
			if (B)
			{
				const FTierShape& SB = Shapes[B->Tier];
				RbX = C.PX - B->X;
				RbY = C.PY - B->Y;
				VbX = B->VX - B->W * RbY;
				VbY = B->VY + B->W * RbX;
				InvMB = SB.InvMass;
				InvIB = SB.InvInertia;
			}

			// 상대 속도 (B - A). 벽이면 B는 멈춰 있다
			const float RvX = VbX - VaX, RvY = VbY - VaY;
			const float Vn = RvX * C.NX + RvY * C.NY;
			if (Vn > 0.f)
			{
				return; // 이미 멀어지는 중
			}

			if (bFirst)
			{
				NoteLanding(A, -Vn);
				if (B)
				{
					NoteLanding(*B, -Vn);
				}
			}

			const float RaN = RaX * C.NY - RaY * C.NX;
			const float RbN = RbX * C.NY - RbY * C.NX;
			const float KN = SA.InvMass + InvMB + RaN * RaN * SA.InvInertia + RbN * RbN * InvIB;
			const float E = (bFirst && -Vn > P.BounceMinSpeed) ? P.Restitution : 0.f;
			const float Jn = -(1.f + E) * Vn / KN;
			ApplyImpulse(A, B, RaX, RaY, RbX, RbY, C.NX * Jn, C.NY * Jn);

			// 마찰 (접선 방향)
			const float VaX2 = A.VX - A.W * RaY, VaY2 = A.VY + A.W * RaX;
			const float VbX2 = B ? B->VX - B->W * RbY : 0.f, VbY2 = B ? B->VY + B->W * RbX : 0.f;
			const float TX = -C.NY, TY = C.NX;
			const float Vt = (VbX2 - VaX2) * TX + (VbY2 - VaY2) * TY;
			const float RaT = RaX * TY - RaY * TX;
			const float RbT = RbX * TY - RbY * TX;
			const float KT = SA.InvMass + InvMB + RaT * RaT * SA.InvInertia + RbT * RbT * InvIB;
			const float Mu = B ? P.Friction : P.WallFriction;
			float Jt = -Vt / KT;
			const float MaxJt = Mu * Jn;
			Jt = std::fmax(-MaxJt, std::fmin(MaxJt, Jt));
			ApplyImpulse(A, B, RaX, RaY, RbX, RbY, TX * Jt, TY * Jt);
		}

		/** B 쪽에 +J, A 쪽에 -J */
		void ApplyImpulse(FBody& A, FBody* B, float RaX, float RaY, float RbX, float RbY, float JX, float JY)
		{
			const FTierShape& SA = Shapes[A.Tier];
			A.VX -= JX * SA.InvMass;
			A.VY -= JY * SA.InvMass;
			A.W -= (RaX * JY - RaY * JX) * SA.InvInertia;
			if (B)
			{
				const FTierShape& SB = Shapes[B->Tier];
				B->VX += JX * SB.InvMass;
				B->VY += JY * SB.InvMass;
				B->W += (RbX * JY - RbY * JX) * SB.InvInertia;
			}
		}

		void FixPosition(const FContact& C)
		{
			const float Fix = std::fmax(C.Pen - P.Slop, 0.f) * P.PositionFix;
			if (Fix <= 0.f)
			{
				return;
			}
			FBody& A = Bodies[C.A];
			const float InvA = Shapes[A.Tier].InvMass;
			if (C.B < 0)
			{
				A.X -= C.NX * Fix;
				A.Y -= C.NY * Fix;
				return;
			}
			FBody& B = Bodies[C.B];
			const float InvB = Shapes[B.Tier].InvMass;
			const float Sum = InvA + InvB;
			A.X -= C.NX * Fix * InvA / Sum;
			A.Y -= C.NY * Fix * InvA / Sum;
			B.X += C.NX * Fix * InvB / Sum;
			B.Y += C.NY * Fix * InvB / Sum;
		}
	};
}
