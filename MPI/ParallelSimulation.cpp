#include "ParallelSimulation.h"

ParallelSimulation::ParallelSimulation(Net *const _net, double _I, double _J, double _decay, double _dt) : Simulation(_net, _I, _J, _decay, _dt)
{
    cols = net->get_cols();
    rows = net->get_rows();
    layers = net->get_layers();
}

void ParallelSimulation::single_simulation_step()
{
    exchange_borders();
    angular_valocity_half_step();
    new_angle();
    exchange_borders();
    update_angular_velocity();
}

void ParallelSimulation::simulation(int steps)
{
    for (int i = 0; i < steps; i++)
        single_simulation_step();
}

void ParallelSimulation::exchange_borders()
{
    exchange_border(start_id, end_id + 1, -1);
    exchange_border(end_id, start_id - 1, 1);
}

void ParallelSimulation::exchange_border(int send_id, int save_id, int offset)
{
    int destination = (((my_rank + offset) % processes) + processes) % processes;
    int source = (((my_rank - offset) % processes) + processes) % processes;
    double **output = alloc_arr<double>(sizes[1], sizes[2]);
    double **input = alloc_arr<double>(sizes[1], sizes[2]);

    for (int i = 0; i < sizes[1]; i++)
    {
        for (int j = 0; j < sizes[2]; j++)
        {
            switch (divide_by)
            {
            case COLS:
                output[i][j] = net->get_angle(send_id, i, j);
                break;
            case ROWS:
                output[i][j] = net->get_angle(j, send_id, i);
                break;
            case LAYERS:
                output[i][j] = net->get_angle(i, j, send_id);
                break;
            }
        }
    }

    if (!my_rank)
    {
        MPI_Recv(&(input[0][0]), sizes[1] * sizes[2], MPI_DOUBLE, source, source, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        MPI_Send(&(output[0][0]), sizes[1] * sizes[2], MPI_DOUBLE, destination, my_rank, MPI_COMM_WORLD);
    }
    else
    {
        MPI_Send(&(output[0][0]), sizes[1] * sizes[2], MPI_DOUBLE, destination, my_rank, MPI_COMM_WORLD);
        MPI_Recv(&(input[0][0]), sizes[1] * sizes[2], MPI_DOUBLE, source, source, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    }

    for (int i = 0; i < sizes[1]; i++)
    {
        for (int j = 0; j < sizes[2]; j++)
        {
            switch (divide_by)
            {
            case COLS:
                net->set_angle(save_id, i, j, input[i][j]);
                break;
            case ROWS:
                net->set_angle(j, save_id, i, input[i][j]);
                break;
            case LAYERS:
                net->set_angle(i, j, save_id, input[i][j]);
                break;
            }
        }
    }

    free(output[0]);
    free(output);
    free(input[0]);
    free(input);
}

double ParallelSimulation::calc_potential_energy()
{
    double result = 0;
    for (int i = start_id; i <= end_id; i++)
    {
        for (int j = 0; j < sizes[1]; j++)
        {
            for (int k = 0; k < sizes[2]; k++)
            {
                switch (divide_by)
                {
                case COLS:
                    result += physics->local_potential_energy(i, j, k);
                    break;
                case ROWS:
                    result += physics->local_potential_energy(k, i, j);
                    break;
                case LAYERS:
                    result += physics->local_potential_energy(j, k, i);
                    break;
                }
            }
        }
    }
    if (!my_rank)
    {
        double received_result = 0;
        for (int i = 1; i < processes; i++)
        {
            MPI_Recv(&received_result, 1, MPI_DOUBLE, i, i, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            result += received_result;
        }
    }
    else
    {
        MPI_Send(&result, 1, MPI_DOUBLE, 0, my_rank, MPI_COMM_WORLD);
    }

    return -J * 0.5 * result;
}

double ParallelSimulation::calc_kinetic_energy()
{
    double result = 0;
    for (int i = start_id; i <= end_id; i++)
    {
        for (int j = 0; j < sizes[1]; j++)
        {
            for (int k = 0; k < sizes[2]; k++)
            {
                switch (divide_by)
                {
                case COLS:
                    result += physics->local_kinetic_energy(i, j, k);
                    break;
                case ROWS:
                    result += physics->local_kinetic_energy(k, i, j);
                    break;
                case LAYERS:
                    result += physics->local_kinetic_energy(j, k, i);
                    break;
                }
            }
        }
    }
    if (!my_rank)
    {
        double received_result = 0;
        for (int i = 1; i < processes; i++)
        {
            MPI_Recv(&received_result, 1, MPI_DOUBLE, i, i, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            result += received_result;
        }
    }
    else
    {
        MPI_Send(&result, 1, MPI_DOUBLE, 0, my_rank, MPI_COMM_WORLD);
    }

    return 0.5 * I * result;
}

void ParallelSimulation::initialize()
{

    MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &processes);

    dimension order[3];
    order[0] = (cols > rows) ? (cols > layers ? COLS : LAYERS) : (rows > layers) ? ROWS
                                                                                 : LAYERS;
    order[1] = dimension((order[0] + 1) % 3);
    order[2] = dimension((order[1] + 1) % 3);
    divide_by = order[0];

    for (int i = 0; i < 3; i++)
        sizes[i] = order[i] == COLS ? cols : order[i] == ROWS ? rows
                                                              : layers;

    chunk_size = sizes[0] / processes;

    start_id = my_rank * chunk_size;
    if (my_rank != processes - 1)
        end_id = ((my_rank + 1) * chunk_size) - 1;
    else
        end_id = sizes[0] - 1;

    double **angles = alloc_arr<double>(sizes[1], sizes[2]);
    double **velocities = alloc_arr<double>(sizes[1], sizes[2]);
    int destination = 0;
    for (int i = chunk_size; i < sizes[0]; i++)
    {
        if (i % chunk_size == 0 && destination != processes - 1)
        {
            destination++;
        }

        if (!my_rank)
        {
            for (int j = 0; j < sizes[1]; j++)
            {
                for (int k = 0; k < sizes[2]; k++)
                {
                    switch (order[0])
                    {
                    case COLS:
                        angles[j][k] = net->get_angle(i, j, k);
                        velocities[j][k] = net->get_angular_velocity(i, j, k);
                        break;
                    case ROWS:
                        angles[j][k] = net->get_angle(k, i, j);
                        velocities[j][k] = net->get_angular_velocity(k, i, j);
                        break;
                    case LAYERS:
                        angles[j][k] = net->get_angle(j, k, i);
                        velocities[j][k] = net->get_angular_velocity(j, k, i);
                        break;
                    }
                }
            }
            MPI_Send(&(angles[0][0]), sizes[1] * sizes[2], MPI_DOUBLE, destination, i, MPI_COMM_WORLD);
            MPI_Send(&(velocities[0][0]), sizes[1] * sizes[2], MPI_DOUBLE, destination, sizes[0] + i, MPI_COMM_WORLD);
        }
        else if (my_rank == destination)
        {
            MPI_Recv(&(angles[0][0]), sizes[1] * sizes[2], MPI_DOUBLE, 0, i, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            MPI_Recv(&(velocities[0][0]), sizes[1] * sizes[2], MPI_DOUBLE, 0, sizes[0] + i, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            for (int j = 0; j < sizes[1]; j++)
            {
                for (int k = 0; k < sizes[2]; k++)
                {
                    switch (order[0])
                    {
                    case COLS:
                        net->set_angle(i, j, k, angles[j][k]);
                        net->set_angular_velocity(i, j, k, velocities[j][k]);
                        break;
                    case ROWS:
                        net->set_angle(k, i, j, angles[j][k]);
                        net->set_angular_velocity(k, i, j, velocities[j][k]);
                        break;
                    case LAYERS:
                        net->set_angle(j, k, i, angles[j][k]);
                        net->set_angular_velocity(j, k, i, velocities[j][k]);
                        break;
                    }
                }
            }
        }
    }

    free(angles[0]);
    free(angles);
    free(velocities[0]);
    free(velocities);
    exchange_borders();
}

template <typename T>
T **ParallelSimulation::alloc_arr(int x, int y)
{
    T *all = (T *)malloc(x * y * sizeof(T));
    T **array = (T **)malloc(x * sizeof(T *));
    for (int i = 0; i < x; i++){
        array[i] = &(all[y * i]);
    } 
    return array;
}

double ParallelSimulation::total_potential_energy()
{
    return potential_energy;
}

double ParallelSimulation::total_kinetic_energy()
{
    return kinetic_energy;
}

void ParallelSimulation::calc_energy()
{
    exchange_borders();
    potential_energy = calc_potential_energy();
    kinetic_energy = calc_kinetic_energy();
}

void ParallelSimulation::angular_valocity_half_step()
{
    for (int i = start_id; i <= end_id; i++)
    {
        for (int j = 0; j < sizes[1]; j++)
        {
            for (int k = 0; k < sizes[2]; k++)
            {
                switch (divide_by)
                {
                case COLS:
                    net->set_angular_velocity(i, j, k,
                                              net->get_angular_velocity(i, j, k) +
                                                  reduced_time * physics->torque(i, j, k));
                    break;
                case ROWS:
                    net->set_angular_velocity(k, i, j,
                                              net->get_angular_velocity(k, i, j) +
                                                  reduced_time * physics->torque(k, i, j));
                    break;
                case LAYERS:
                    net->set_angular_velocity(j, k, i,
                                              net->get_angular_velocity(j, k, i) +
                                                  reduced_time * physics->torque(j, k, i));
                    break;
                }
            }
        }
    }
}

void ParallelSimulation::new_angle()
{
    for (int i = start_id; i <= end_id; i++)
    {
        for (int j = 0; j < sizes[1]; j++)
        {
            for (int k = 0; k < sizes[2]; k++)
            {
                switch (divide_by)
                {
                case COLS:
                    net->set_angle(
                        i, j, k,
                        net->get_angle(i, j, k) +
                            dt * net->get_angular_velocity(i, j, k));
                    break;
                case ROWS:
                    net->set_angle(
                        k, i, j,
                        net->get_angle(k, i, j) +
                            dt * net->get_angular_velocity(k, i, j));
                    break;
                case LAYERS:
                    net->set_angle(
                        j, k, i,
                        net->get_angle(j, k, i) +
                            dt * net->get_angular_velocity(j, k, i));
                    break;
                }
            }
        }
    }
}

void ParallelSimulation::update_angular_velocity()
{
    for (int i = start_id; i <= end_id; i++)
    {
        for (int j = 0; j < sizes[1]; j++)
        {
            for (int k = 0; k < sizes[2]; k++)
            {
                switch (divide_by)
                {
                case COLS:
                    net->set_angular_velocity(
                        i, j, k,
                        net->get_angular_velocity(i, j, k) +
                            reduced_time * physics->torque(i, j, k));
                    break;
                case ROWS:
                    net->set_angular_velocity(
                        k, i, j,
                        net->get_angular_velocity(k, i, j) +
                            reduced_time * physics->torque(k, i, j));
                    break;
                case LAYERS:
                    net->set_angular_velocity(
                        j, k, i,
                        net->get_angular_velocity(j, k, i) +
                            reduced_time * physics->torque(j, k, i));
                    break;
                }
            }
        }
    }
}

void ParallelSimulation::angles_and_velocities_to_proc_0()
{
    double **angles = alloc_arr<double>(sizes[1], sizes[2]);
    double **velocities = alloc_arr<double>(sizes[1], sizes[2]);

    if (!my_rank)
    {
        int source = 0;
        for (int i = chunk_size; i < sizes[0]; i++)
        {
            if (i % chunk_size == 0 && source != processes - 1)
            {
                source++;
            }

            MPI_Recv(&(angles[0][0]), sizes[1] * sizes[2], MPI_DOUBLE, source, i, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            MPI_Recv(&(velocities[0][0]), sizes[1] * sizes[2], MPI_DOUBLE, source, sizes[0] + i, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            for (int j = 0; j < sizes[1]; j++)
            {
                for (int k = 0; k < sizes[2]; k++)
                {
                    switch (divide_by)
                    {
                    case COLS:
                        net->set_angle(i, j, k, angles[j][k]);
                        net->set_angular_velocity(i, j, k, velocities[j][k]);
                        break;
                    case ROWS:
                        net->set_angle(k, i, j, angles[j][k]);
                        net->set_angular_velocity(k, i, j, velocities[j][k]);
                        break;
                    case LAYERS:
                        net->set_angle(j, k, i, angles[j][k]);
                        net->set_angular_velocity(j, k, i, velocities[j][k]);
                        break;
                    }
                }
            }
        }
    }
    else
    {
        for (int i = start_id; i <= end_id; i++)
        {
            for (int j = 0; j < sizes[1]; j++)
            {
                for (int k = 0; k < sizes[2]; k++)
                {
                    switch (divide_by)
                    {
                    case COLS:
                        angles[j][k] = net->get_angle(i, j, k);
                        velocities[j][k] = net->get_angular_velocity(i, j, k);
                        break;
                    case ROWS:
                        angles[j][k] = net->get_angle(k, i, j);
                        velocities[j][k] = net->get_angular_velocity(k, i, j);
                        break;
                    case LAYERS:
                        angles[j][k] = net->get_angle(j, k, i);
                        velocities[j][k] = net->get_angular_velocity(j, k, i);
                        break;
                    }
                }
            }

            MPI_Send(&(angles[0][0]), sizes[1] * sizes[2], MPI_DOUBLE, 0, i, MPI_COMM_WORLD);
            MPI_Send(&(velocities[0][0]), sizes[1] * sizes[2], MPI_DOUBLE, 0, sizes[0] + i, MPI_COMM_WORLD);
        }
    }

    free(angles[0]);
    free(angles);
    free(velocities[0]);
    free(velocities);
}