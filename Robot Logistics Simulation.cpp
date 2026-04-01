# include <Siv3D.hpp>

// --- 定数・列挙型 ---
enum class RobotStatus { Moving, Loading };
const Size GridSize{ 16, 8 };
const double CellSize = 50.0;

// --- 統計管理クラス ---
struct Stats {
	uint32 totalProcessed = 0;
	Array<int32> lapHistory;
	double getAverageLaps() const {
		if (lapHistory.isEmpty()) return 0.0;
		return (double)lapHistory.sum() / lapHistory.size();
	}
};

// --- ロボットクラス ---
struct Robot {
	size_t id;
	Point gridPos;
	Vec2 drawPos;
	Optional<Color> cargo;
	RobotStatus status = RobotStatus::Moving;
	Timer loadingTimer{ 1.0s };
	int32 currentLaps = 0;
	bool isStuck = false;

	void update(const Grid<Vec2>& flow, Grid<int32>& reserved, const Grid<Optional<Color>>& stations, Stats& stats) {
		if (status == RobotStatus::Loading) {
			if (loadingTimer.reachedZero()) {
				stats.totalProcessed++;
				stats.lapHistory << (currentLaps + 1);
				cargo = none;
				status = RobotStatus::Moving;
				currentLaps = 0;
			}
			return;
		}

		// 次の移動先を決定
		Point next = gridPos + flow[gridPos].asPoint();
		if (next.x < gridPos.x) currentLaps++; // 周回カウント

		// 周囲に自分の荷物と同じ色のステーションがあるか判定（簡易A*的な分岐）
		for (Point delta : { Point{ 0, 1 }, Point{ 0, -1 }, Point{ 1, 0 }, Point{ -1, 0 } }) {
			Point neighbor = gridPos + delta;
			if (stations.inBounds(neighbor) && stations[neighbor] == cargo && reserved[neighbor] == -1) {
				next = neighbor;
				break;
			}
		}

		// 移動実行（衝突回避：予約システム）
		if (reserved.inBounds(next) && reserved[next] == -1) {
			reserved[gridPos] = -1; // 元の場所を解放
			gridPos = next;
			reserved[gridPos] = (int32)id; // 新しい場所を予約
			isStuck = false;

			// ステーションに到着
			if (stations[gridPos].has_value()) {
				status = RobotStatus::Loading;
				loadingTimer.restart();
			}
		}
		else {
			isStuck = true; // 渋滞中
		}

		// 荷物の補充（左端の特定の列を補充エリアとする）
		if (!cargo && gridPos.x == 0) {
			cargo = Sample({ Palette::Red, Palette::Cyan, Palette::Lime });
		}

		drawPos = drawPos.lerp(gridPos * CellSize + Vec2{ 25, 25 }, 0.2);
	}

	void draw() const {
		Circle{ drawPos, 18 }.draw(Palette::White).drawFrame(2, isStuck ? Palette::Red : Palette::Black);
		if (cargo) RectF{ Arg::center = drawPos, 15 }.draw(*cargo).drawFrame(1, Palette::Black);
		if (status == RobotStatus::Loading) {
			RectF{ Arg::center = drawPos + Vec2{0, -22}, 30 * loadingTimer.progress0_1(), 4}.draw(Palette::Yellow);
		}
	}
};

void Main() {
	Window::Resize(800, 600);
	const Font font{ 20, Typeface::Bold };

	// グリッドデータ
	Grid<Vec2> flow(GridSize, Vec2{ 1, 0 }); // 基本は右移動
	Grid<int32> reserved(GridSize, -1);      // -1: 空き, ID: 予約中
	Grid<Optional<Color>> stations(GridSize, none);
	Grid<double> heatMap(GridSize, 0.0);

	// デフォルトのレイアウト（外周ループ）
	for (int y = 0; y < GridSize.y; ++y) flow[y][GridSize.x - 1] = Vec2{ -(GridSize.x - 1), 0 };
	stations[1][6] = Palette::Red;
	stations[6][10] = Palette::Cyan;
	stations[1][14] = Palette::Lime;

	// シミュレーション変数
	Array<Robot> robots;
	Stats stats;
	double robotCountTarget = 15.0;
	int32 simSpeed = 1;
	bool showHeatMap = true;
	size_t selectedColorIdx = 0;
	const Array<Color> palette = { Palette::Red, Palette::Cyan, Palette::Lime };

	while (System::Update()) {
		// --- 1. エディタ操作 ---
		const Point mPos = Cursor::Pos() / (int32)CellSize;
		if (stations.inBounds(mPos)) {
			if (MouseL.pressed()) stations[mPos] = palette[selectedColorIdx];
			if (MouseR.pressed()) stations[mPos] = none;
		}

		// --- 2. 台数管理 ---
		while (robots.size() < (size_t)robotCountTarget) {
			Point p{ Random(GridSize.x - 1), Random(GridSize.y - 1) };
			if (reserved[p] == -1) {
				robots.push_back({ robots.size(), p, p * CellSize + Vec2{25,25}, none });
				reserved[p] = (int32)robots.back().id;
			}
		}
		if (robots.size() > (size_t)robotCountTarget) {
			reserved[robots.back().gridPos] = -1;
			robots.pop_back();
		}

		// --- 3. ロジック更新 (倍速対応) ---
		for (int32 i = 0; i < simSpeed; ++i) {
			if (Periodic::Jump0_1(0.1s) < Scene::DeltaTime() * 10) {
				for (auto& r : robots) {
					r.update(flow, reserved, stations, stats);
					if (r.isStuck) heatMap[r.gridPos] += 0.5;
				}
			}
			for (auto& h : heatMap) h *= 0.998; // 自然減衰
		}

		// --- 4. 描画 ---
		for (auto p : step(GridSize)) {
			RectF rect{ p * CellSize, CellSize };
			if (showHeatMap) {
				double x = Min(heatMap[p] / 15.0, 1.0);
				rect.draw(ColorF{ 1.0, 1.0 - x, 1.0 - x });
			}
			else {
				rect.draw(Palette::White);
			}
			rect.drawFrame(1, Palette::Lightgray);
			if (stations[p]) rect.draw(AlphaF(0.3)).drawFrame(3, 0, *stations[p]);
		}
		for (const auto& r : robots) r.draw();

		// --- 5. UIパネル ---
		Rect{ 0, 400, 800, 200 }.draw(Color{ 35 });
		SimpleGUI::Slider(U"Count: {}"_fmt(robots.size()), robotCountTarget, 1, 60, { 20, 420 }, 140, 180);
		SimpleGUI::RadioButtons(selectedColorIdx, { U"Red", U"Blue", U"Green" }, { 360, 420 });
		SimpleGUI::CheckBox(showHeatMap, U"Heatmap", { 20, 460 });

		if (SimpleGUI::Button(U"Speed x1", { 550, 420 }, 100)) simSpeed = 1;
		if (SimpleGUI::Button(U"Speed x8", { 660, 420 }, 100)) simSpeed = 8;
		if (SimpleGUI::Button(U"Reset Stats", { 550, 470 }, 210)) { stats = Stats{}; heatMap.fill(0.0); }

		font(U"Total Processed: {}"_fmt(stats.totalProcessed)).draw(20, 500);
		font(U"Avg Laps to Deliver: {:.2f}"_fmt(stats.getAverageLaps())).draw(20, 530);
	}
}
