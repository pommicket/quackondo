#ifndef MACONDO_H
#define MACONDO_H

#include "view.h"
#include "game.h"

class QCheckBox;
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
public slots:
	void simulate();
	virtual void gameChanged(Quackle::Game *game);
	virtual void positionChanged(const Quackle::GamePosition *position);
private slots:
	void gotSimMoves(const Quackle::MoveList &moves);
private:
	QCheckBox *m_useMacondo;
	Quackle::Game *m_game;
	MacondoBackend *m_backend;
	Quackle::MoveList m_moves;
	Quackle::MoveList m_movesFromKibitzer;
	int m_viewingPlyNumber = 0;
	bool m_anyUpdates = false;
	std::unique_ptr<MacondoInitOptions> initOptions;
};

#endif
