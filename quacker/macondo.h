#ifndef MACONDO_H
#define MACONDO_H

#include "view.h"
#include "game.h"

class QCheckBox;
class QPushButton;
class QLineEdit;
class MacondoBackend;
struct MacondoInitOptions;
class MoveBox;

class Macondo : public View {
Q_OBJECT
public:
	Macondo(Quackle::Game *game);
	~Macondo();
	// stop current analysis
	void stop();
	// should Macondo be used for simulations?
	bool useForSimulation() const;
	inline const Quackle::MoveList &getMoves() const { return m_moves; }
	inline bool hasMoves() const { return !m_moves.empty(); }
	inline void clearMoves() { m_moves.clear(); }
	// returns whether there have been any updates to moves since last time anyUpdates() was called.
	inline bool anyUpdates() {
		bool any = m_anyUpdates;
		m_anyUpdates = false;
		return any;
	}
	bool isRunning() const;
	inline bool isSolving() const { return m_isSolving; }
signals:
	void runningSolver();
	void stoppedSolver();
public slots:
	void simulate();
	void solve();
	void gameChanged(Quackle::Game *game) override;
	void positionChanged(const Quackle::GamePosition *position) override;
private slots:
	void gotMoves(const Quackle::MoveList &moves);
	void newExecPath();
private:
	void connectBackendSignals();
	void updateSolveButton();
	QCheckBox *m_useMacondo;
	QCheckBox *m_generatedMovesOnly;
	QLineEdit *m_execPath;
	QPushButton *m_solve;
	Quackle::Game *m_game;
	MacondoBackend *m_backend;
	Quackle::MoveList m_moves;
	Quackle::MoveList m_movesFromKibitzer;
	int m_tilesUnseen = 93;
	int m_viewingPlyNumber = 0;
	bool m_anyUpdates = false;
	bool m_isSolving = false;
	std::unique_ptr<MacondoInitOptions> m_initOptions;
};

#endif
