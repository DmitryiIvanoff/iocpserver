#pragma once

class IRoutine {
public:
	virtual ~IRoutine() {};

	HANDLE GetIOCP() const;

	void SetHelperIOCP(const HANDLE compPort);

};