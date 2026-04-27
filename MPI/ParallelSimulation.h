#ifndef PAR_SIMULATION_H_

#define PAR_SIMULATION_H_

#include "Simulation.h"
#include <mpi.h>

class ParallelSimulation : public Simulation
{
private:
    enum dimension
    {
        COLS = 0,
        ROWS,
        LAYERS
    };
    int cols, rows, layers;
    int my_rank, processes;
    int start_id, end_id;
    dimension divide_by;
    int sizes[3];
    int chunk_size;
    double potential_energy, kinetic_energy;

    void angular_valocity_half_step();
    void new_angle();
    void update_angular_velocity();
    void single_simulation_step();
    void exchange_borders();
    void exchange_border(int send_id, int save_id, int offset);
    double calc_potential_energy();
    double calc_kinetic_energy();
    template <typename T>
    T **alloc_arr(int x, int y);

public:
    ParallelSimulation(Net *const _net, double _I, double _J, double _decay, double _dt);

    void simulation(int steps);
    double total_potential_energy();
    double total_kinetic_energy();

    void calc_energy();
    void initialize();
    void angles_and_velocities_to_proc_0();
};

#endif