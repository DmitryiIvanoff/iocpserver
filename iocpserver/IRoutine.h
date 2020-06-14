#pragma once

class IRoutine {
public:
	virtual ~IRoutine() {};

	virtual HANDLE GetIOCP() const = 0;

	virtual void SetHelperIOCP(const HANDLE compPort) = 0;

};